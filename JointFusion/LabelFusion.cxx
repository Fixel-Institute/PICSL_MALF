#include "WeightedVotingLabelFusionImageFilter.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include <iostream>

#include "WeightedVotingLabelFusionImageFilter.txx"

using namespace std;

int usage()
{
  cout << "Joint Label Fusion: " << endl;
  cout << "usage: " << endl;
  cout << " jointfusion dim mod [options] output_image" << endl;
  cout << endl;
  cout << "required options:" << endl;
  cout << "  dim                             Image dimension (2 or 3)" << endl;
  cout << "  mod                             Number of modalities or features" << endl;
  cout << "  -g atlas1_mod1.nii atlas1_mod2.nii ...atlasN_mod1.nii atlasN_mod2.nii ... "<<endl;
  cout << "                                  Warped atlas images" << endl;
  cout << "  -tg target_mod1.nii ... target_modN.nii "<<endl;
  cout << "                                  Target image(s)" << endl;
  cout << "  -l label1.nii ... labelN.nii    Warped atlas segmentation" << endl;
  cout << "  -m <method> [parameters]        Select voting method. Options: Joint (Joint Label Fusion) " << endl;
  cout << "                                  May be followed by optional parameters in brackets, e.g., -m Joint[0.1,2]."<<endl; 
  cout << "                                  See below for parameters" << endl;
  cout << "other options: " << endl;
  cout << "  -rp radius                      Patch radius for similarity measures, scalar or vector (AxBxC) " << endl;
  cout << "                                  Default: 2x2x2" << endl;
  cout << "  -rs radius                      Local search radius." << endl;
  cout << "                                  Default: 3x3x3" << endl;
  cout << "  -x label image.nii              Specify an exclusion region for the given label." << endl;
  cout << "  -p filenamePattern              Save the posterior maps (probability that each voxel belongs to each label) as images."<<endl;
  cout << "                                  The number of images saved equals the number of labels."<<endl;
  cout << "                                  The filename pattern must be in C printf format, e.g. posterior%04d.nii.gz" << endl;
  cout << "Parameters for -m Joint option:" << endl;
  cout << "  alpha                           Regularization term added to matrix Mx for inverse" << endl;
  cout << "                                  Default: 0.1" << endl;
  cout << "  beta                            Exponent for mapping intensity difference to joint error" << endl;
  cout << "                                  Default: 2" << endl;

  return -1;
}

enum LFMethod
{
  JOINT, GAUSSIAN, INVERSE 
};

template<unsigned int VDim> 
struct LFParam
{
  vector<string> fnAtlas;
  vector<string> fnLabel;
  vector<string> fnTargetImage;
  string fnOutput;
  string fnPosterior;
  LFMethod method;

  map<int, string> fnExclusion;

  int modality;
  double alpha, beta, sigma;
  itk::Size<VDim> r_patch, r_search;
  
  LFParam()
    {
    alpha = 0.1;
    beta = 2;
    sigma = 0.5;
    modality = 1;
    r_patch.Fill(2);
    r_search.Fill(3);
    method = JOINT;
    }

  void Print(std::ostream &oss)
    {
    oss << "modality number: " << modality << endl;
    for (size_t i=0;i<fnTargetImage.size();i++)
      oss << "Target image: " << fnTargetImage[i] << endl;
    oss << "Output image: " << fnOutput << endl;
    oss << "Atlas images: " << fnAtlas.size()<<endl;
    for(size_t i = 0; i < fnLabel.size(); i++)
      {
      oss << "    " << i << "\t";
      for (int j=0;j<modality;j++)
        oss << " : " << fnAtlas[i*modality+j];

      oss << " | " << fnLabel[i] << endl;
      }

    if(method == GAUSSIAN)
      {
      oss << "Method:     Gaussian" << endl;
      oss << "    Sigma:  " << sigma << endl;
      }
    else if(method == JOINT)
      {
      oss << "Method:     Joint" << endl;
      oss << "    Alpha:  " << alpha << endl;
      oss << "    Beta:  " << beta << endl;
      }
    else if(method == INVERSE)
      {
      oss << "Method:     Inverse" << endl;
      oss << "    Beta:  " << beta << endl;
      }
    oss << "Search Radius: " << r_search << endl;
    oss << "Patch Radius: " << r_patch << endl;
    if(fnPosterior.size())
      oss << "Posterior Filename Pattern: " << fnPosterior << endl;
    }
};


template <unsigned int VDim>
void ExpandRegion(itk::ImageRegion<VDim> &r, bool &isinit, const itk::Index<VDim> &idx)
{
  if(!isinit)
    {
    for(size_t d = 0; d < VDim; d++)
      {
      r.SetIndex(d, idx[d]);
      r.SetSize(d,1);
      }
    isinit = true;
    }
  else
    {
    for(size_t d = 0; d < VDim; d++)
      {
      int x = r.GetIndex(d), s = r.GetSize(d);
      if(idx[d] < x)
        {
        r.SetSize(d, s + x - idx[d]);
        r.SetIndex(d, idx[d]);
        }
      else if(idx[d] >= x + s)
        {
        r.SetSize(d, 1 + idx[d] - x);
        } 
      }
    }
}


template<unsigned int VDim>
bool
parse_vector(char *text, itk::Size<VDim> &s)
{
  char *t = strtok(text,"x");
  size_t i = 0;
  while(t && i < VDim)
    {
    s[i++] = atoi(t);
    t = strtok(NULL, "x");
    }

  if(i == VDim)
    return true;
  else if(i == 1)
    {
    s.Fill(s[0]);
    return true;
    }
  return false;
}

template <unsigned int VDim>
int lfapp(int argc, char *argv[])
{
  // Parameter vector
  LFParam<VDim> p;

  // Read the parameters from command line
  p.fnOutput = argv[argc-1];
//  p.fnTarget = argv[argc-2];
  int argend = argc-2;
  p.modality=atoi(argv[2]);
  std::cout<<"mod: "<<p.modality<<std::endl;

  for(int j = 3; j < argc-2; j++)
    {
    string arg = argv[j];
    
    if(arg == "-g")
      {
      // Read the following options as images
      while(argv[j+1][0] != '-' && j < argend)
        p.fnAtlas.push_back(argv[++j]);
      }
    else if(arg == "-tg")
      {
      // Read the following options as images
      while(argv[j+1][0] != '-' && j < argend)
        p.fnTargetImage.push_back(argv[++j]);
      }

    else if(arg == "-l")
      {
      // Read the following options as images
      while(argv[j+1][0] != '-' && j < argend)
        p.fnLabel.push_back(argv[++j]);
      }

    else if(arg == "-p")
      {
      p.fnPosterior = argv[++j];
      }

    else if(arg == "-x")
      {
      int label = atoi(argv[++j]);
      string image = argv[++j];
      p.fnExclusion[label] = image;
      }

    else if(arg == "-m" && j < argend)
      {
      char *parm = argv[++j];
      if(!strncmp(parm, "Joint", 5))
        {
        float alpha, beta;
        switch(sscanf(parm, "Joint[%f,%f]", &alpha, &beta))
          {
        case 2:
          p.alpha = alpha; p.beta = beta; break;
        case 1:
          p.alpha = alpha; break;
          }
        p.method = JOINT;
        }
      else
        {
        cerr << "Unknown method specification " << parm << endl;
        return -1;
        }
      }

    else if(arg == "-rs" && j < argend)
      {
      if(!parse_vector<VDim>(argv[++j], p.r_search))
        {
        cerr << "Bad vector spec " << argv[j] << endl;
        return -1;
        }
      }
    
    else if(arg == "-rp" && j < argend)
      {
      if(!parse_vector<VDim>(argv[++j], p.r_patch))
        {
        cerr << "Bad vector spec " << argv[j] << endl;
        return -1;
        }
      }

    else
      {
      cerr << "Unknown option " << arg << endl;
      return -1;
      }
    }


  // We have the parameters now. Check for validity
  if(p.fnAtlas.size()/p.modality != p.fnLabel.size())
    {
    cerr << "Number of atlases and segmentations does not match!"<<p.fnAtlas.size()/p.modality<<"atlas images "<<p.fnLabel.size()<<"segmentatons" << endl;
    return -1;
    }

  if(p.fnAtlas.size()/p.modality < 2)
    {
    cerr << "Too few atlases" << endl;
    return -1;
    }

  // Check the posterior filename pattern
  if(p.fnPosterior.size())
    {
    char buffer[4096];
    sprintf(buffer, p.fnPosterior.c_str(), 100);
    if(strcmp(buffer, p.fnPosterior.c_str()) == 0)
      {
      cerr << "Invalid filename pattern " << p.fnPosterior << endl;
      return -1;
      }
    }

  // Print parametes
  cout << "LABEL FUSION PARAMETERS:" << endl;
  p.Print(cout);

  // Configure the filter
  typedef itk::Image<float, VDim> ImageType;
  typedef WeightedVotingLabelFusionImageFilter<ImageType, ImageType> VoterType;
  typename VoterType::Pointer voter = VoterType::New();

  // Set inputs
  typedef itk::ImageFileReader<ImageType> ReaderType;
  typedef itk::ImageFileWriter<ImageType> WriterType;

//  typename ReaderType::Pointer rTarget = ReaderType::New();
//  rTarget->SetFileName(p.fnTarget.c_str());
//  rTarget->Update();
//  typename ImageType::Pointer target = rTarget->GetOutput();
//  voter->SetTargetImage(target);

  typedef typename ImageType::Pointer  InputImagePointer;
  std::vector<InputImagePointer> targetl;
    for (int j=0;j<p.modality;j++){
      typename ReaderType::Pointer rTarget = ReaderType::New();
      cout<<p.fnTargetImage[j].c_str()<<endl;
      rTarget->SetFileName(p.fnTargetImage[j].c_str());
      rTarget->Update();
      targetl.push_back(rTarget->GetOutput());
    }
  voter->SetTargetImage(targetl);
  typename ImageType::Pointer target = targetl[0];

  
  // Compute the output region by merging all segmentations
  itk::ImageRegion<VDim> rMask;
  bool isMaskInit = false;

  std::vector<typename ReaderType::Pointer> rAtlas, rLabel;
  for(size_t i = 0; i < p.fnLabel.size(); i++)
    {
    typename ReaderType::Pointer ra, rl, ro;
    std::vector<InputImagePointer> ral;
    for (int j=0;j<p.modality;j++){
      ra = ReaderType::New();
      cout<<p.fnAtlas[i*p.modality+j].c_str()<<endl;
      ra->SetFileName(p.fnAtlas[i*p.modality+j].c_str());
      ra->Update();
      ral.push_back(ra->GetOutput());
    }


    rl = ReaderType::New();
    rl->SetFileName(p.fnLabel[i].c_str());
    rl->Update();
    rLabel.push_back(rl);
    voter->AddAtlas(ral, rl->GetOutput());

    for(itk::ImageRegionIteratorWithIndex<ImageType>
      it(rl->GetOutput(), rl->GetOutput()->GetRequestedRegion());
      !it.IsAtEnd(); ++it)
      {
      if (1)
        {
        ExpandRegion<VDim>(rMask, isMaskInit, it.GetIndex());
        }
      }
    }

  // Make sure the region is inside bounds
  itk::ImageRegion<VDim> rOut = targetl[0]->GetLargestPossibleRegion();
  for(int d = 0; d < 3; d++)
    {
    rOut.SetIndex(d, p.r_patch[d] + p.r_search[d] + rOut.GetIndex(d));
    rOut.SetSize(d, rOut.GetSize(d) - 2 * (p.r_search[d] + p.r_patch[d]));
    }
  rMask.Crop(rOut);

  voter->SetPatchRadius(p.r_patch);
  voter->SetSearchRadius(p.r_search);
  voter->SetAlpha(p.alpha);
  voter->SetBeta(p.beta);
  voter->SetModality(p.modality);

  // The posterior maps
  if(p.fnPosterior.size())
    voter->SetRetainPosteriorMaps(true);

  std::cout << "Output Requested Region: " << rMask.GetIndex() << ", " << rMask.GetSize() << " ("
    << rMask.GetNumberOfPixels() << " pixels)" << std::endl;
  /*
  rr.SetIndex(0,275); rr.SetSize(0,10);
  rr.SetIndex(1,210); rr.SetSize(1,10);
  rr.SetIndex(2,4); rr.SetSize(2,10);
  */

  // Set the exclusions in the atlas
  for(typename map<int,string>::iterator xit = p.fnExclusion.begin(); xit != p.fnExclusion.end(); ++xit)
    {
    typename ReaderType::Pointer rx;
    rx = ReaderType::New();
    rx->SetFileName(xit->second.c_str());
    rx->Update();
    voter->AddExclusionMap(xit->first, rx->GetOutput());
    }

  voter->GetOutput()->SetRequestedRegion(rMask);
//  voter->Update();
  voter->GenerateData();

  cout<<"saving consensus segmentation ... "<<endl;

  // Convert to an output image
  target->FillBuffer(0.0);
  for(itk::ImageRegionIteratorWithIndex<ImageType> it(voter->GetOutput(), rMask);
    !it.IsAtEnd(); ++it)
    {
    target->SetPixel(it.GetIndex(), it.Get());
    }

  // Create writer
  typename WriterType::Pointer writer = WriterType::New();
  writer->SetInput(target);
  writer->SetFileName(p.fnOutput.c_str());
  writer->Update();

  // Finally, store the posterior maps
  if(p.fnPosterior.size())
    {
    cout<<"saving label posteriors ... "<<endl;
    // Get the posterior maps
    const typename VoterType::PosteriorMap &pm = voter->GetPosteriorMaps();

    // Create a full-size image
    typename VoterType::PosteriorImagePtr pout = VoterType::PosteriorImage::New();
    pout->SetRegions(target->GetBufferedRegion());
    pout->CopyInformation(target);
    pout->Allocate();

    // Iterate over the labels
    typename VoterType::PosteriorMap::const_iterator it;
    for(it = pm.begin(); it != pm.end(); it++)
      {
      // Get the filename
      char buffer[4096];
      sprintf(buffer, p.fnPosterior.c_str(), (int) it->first);

      // Initialize to zeros
      pout->FillBuffer(0.0);

      // Replace the portion affected by the filter
      for(itk::ImageRegionIteratorWithIndex<typename VoterType::PosteriorImage> qt(it->second, rMask);
        !qt.IsAtEnd(); ++qt)
        {
        pout->SetPixel(qt.GetIndex(), qt.Get());
        }

      // Create writer
      typename WriterType::Pointer writer = WriterType::New();
      writer->SetInput(pout);
      writer->SetFileName(buffer);
      writer->Update();
      }
    }

  return 0;
}


int main(int argc, char *argv[])
{
  // Parse user input
  if(argc < 5) return usage();

  // Get the first option
  int dim = atoi(argv[1]);
  
  // Call the templated method
  if(dim == 2)
    return lfapp<2>(argc, argv);
  else if(dim == 3)
    return lfapp<3>(argc, argv);
  else
    {
    cerr << "Dimension " << argv[1] << " is not supported" << endl;
    return -1;
    }
}
