#include "CinemaWriter.h"

#include <vtkDataObject.h>
#include <vtkObjectFactory.h>
#include <vtkInformation.h>

#include <vtkMultiBlockDataSet.h>
#include <vtkImageData.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkDirectory.h>

#include <vtkPNGWriter.h>

#include <algorithm>
// #include <H5Cpp.h>
#include <vtk_hdf5.h>
#include <sstream>
#include <fstream>

vtkStandardNewMacro(CinemaWriter);

//----------------------------------------------------------------------------
CinemaWriter::CinemaWriter(){
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
};
CinemaWriter::~CinemaWriter() = default;

int CinemaWriter::FillInputPortInformation(int port, vtkInformation *info) {
  if(port == 0) {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataObject");
    return 1;
  }
  return 0;
};

int CinemaWriter::FillOutputPortInformation(int port, vtkInformation *info) {
  if(port == 0) {
    info->Set(CinemaAlgorithm::SAME_DATA_TYPE_AS_INPUT_PORT(), 0);
    return 1;
  }
  return 0;
};

int addH5DataSet(const hid_t& group, const std::string& name, const hsize_t dim[3], const void* data, const hsize_t data_type, const int compression){

  hsize_t DIM = dim[2];
  const hid_t dataspace = H5Screate_simple(DIM, dim, NULL);
  const hid_t plist = H5Pcreate(H5P_DATASET_CREATE);
  hsize_t cdims[3]{
    256<dim[0] ? 256 : dim[0],
    32<dim[1] ? 32 : dim[1],
    dim[2]
  };
  H5Pset_chunk(plist, DIM, cdims);
  H5Pset_deflate(plist, compression);

  const hid_t dataset = H5Dcreate(
    group,
    name.data(),
    data_type,
    dataspace,
    H5P_DEFAULT,
    plist,
    H5P_DEFAULT
  );

  H5Dwrite(dataset, data_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);

  H5Dclose(dataset);
  H5Sclose(dataspace);
  H5Pclose(plist);

  return 1;
}

template <typename DT>
int writeArray(const hid_t group, const int compressionLevel, const hid_t data_type, vtkDataArray* array, hsize_t resX=0, hsize_t resY=0){

  int nTuples = array->GetNumberOfTuples();
  int nComponents = array->GetNumberOfComponents();
  if(nTuples<1)
    return 0;

  if(resX>0){
    hsize_t s[3]{resY*resX,(hsize_t)nComponents,2};
    const auto data_ = static_cast<DT*>(array->GetVoidPointer(0));
    std::vector<DT> data(resX*resY*nComponents);
    for(int y=0;y<resY; y++){
      for(int x=0;x<resX; x++){
        const auto i_idx = y*resX + x;
        const auto o_idx = (resY-1-y)*resX + x;
        for(int cIdx=0; cIdx<nComponents; cIdx++){
          data[o_idx*nComponents+cIdx] = data_[i_idx*nComponents+cIdx];
        }
      }
    }
    return addH5DataSet(group,array->GetName(),s,data.data(),data_type,compressionLevel);
  } else {
    hsize_t s[3]{(hsize_t)nTuples,(hsize_t)(nComponents),(hsize_t)nTuples};
    const auto data = static_cast<DT*>(array->GetVoidPointer(0));
    return addH5DataSet(group,array->GetName(),s,data,data_type,compressionLevel);
  }
};

int writeImage(vtkImageData* image, const std::string path, const int compressionLevel){

  const hid_t root = H5Fcreate(path.data(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  const hid_t meta = H5Gcreate(root, "meta", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  const hid_t channels = H5Gcreate(root, "channels", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

  int dims[3];
  image->GetDimensions(dims);

  // write resolution
  {
    hsize_t s[3]{2,0,0};
    float data[2]{(float)dims[0],(float)dims[1]};
    addH5DataSet(meta,"resolution",s,data,H5T_NATIVE_FLOAT,0);
  }

  // // write offset (here always defaults to 0,0)
  // {
  //   hsize_t s[3]{2,1,1};
  //   float data[2]{0,0};
  //   addH5DataSet(meta,"offset",s,data,0);
  // }

  // write meta
  {
    auto arrays = image->GetFieldData();
    for(int i=0; i<arrays->GetNumberOfArrays(); i++){
      auto array = arrays->GetArray(i);
      if(!array)
        continue;
      switch(array->GetDataType()){
        case VTK_FLOAT:
          writeArray<float>(meta,compressionLevel,H5T_NATIVE_FLOAT,array);
          break;
        default:
          std::cout<<"Unsupported Data Type: "<<array->GetName()<<std::endl;
      }
    }
  }

  // write channels
  {
    auto arrays = image->GetPointData();
    for(int i=0; i<arrays->GetNumberOfArrays(); i++){
      auto array = arrays->GetArray(i);
      if(!array)
        continue;
      switch(array->GetDataType()){
        case VTK_FLOAT:
          writeArray<float>(channels,compressionLevel,H5T_NATIVE_FLOAT,array,dims[0],dims[1]);
          break;
        case VTK_UNSIGNED_CHAR:
          writeArray<uint8_t>(channels,compressionLevel,H5T_NATIVE_UCHAR,array,dims[0],dims[1]);
          break;
        default:
          std::cout<<"Unsupported Data Type: "<<array->GetName()<<std::endl;
      }
    }
  }

    // // write channels
    // {
    //   auto arrays = image->GetPointData();
    //   for(int i=0; i<arrays->GetNumberOfArrays(); i++){
    //     auto array = arrays->GetArray(i);
    //     if(!array)
    //       continue;
    //     int nTuples = array->GetNumberOfTuples();
    //     int nComponents = array->GetNumberOfComponents();
    //     if(nTuples<1)
    //       continue;
    //     std::vector<float> data(nTuples*nComponents);
    //     std::vector<double> rawData(nComponents);

    //     for(int y=0;y<dims[1]; y++){
    //       for(int x=0;x<dims[0]; x++){
    //         const auto i_idx = y*dims[0] + x;
    //         const auto o_idx = (dims[1]-1-y)*dims[0] + x;
    //         array->GetTuple(i_idx,rawData.data());
    //         for(int cIdx=0; cIdx<nComponents; cIdx++){
    //           data[o_idx*nComponents+cIdx] = static_cast<float>(rawData[cIdx]);
    //         }
    //       }
    //     }

    //     // for(int tIdx=0; tIdx<nTuples; tIdx++){
    //       // array->GetTuple(tIdx,rawData.data());
    //       // const auto target = nTuples-tIdx-1;
    //       // for(int cIdx=0; cIdx<nComponents; cIdx++){
    //       //   data[target*nComponents+cIdx] = static_cast<float>(rawData[cIdx]);
    //       // }
    //     // }
    //     hsize_t s[3]{(hsize_t)dims[1],(hsize_t)dims[0],(hsize_t)nComponents};
    //     addH5DataSet(channels,array->GetName(),s,data.data(),H5T_NATIVE_FLOAT,compressionLevel);
    //   }
    // }
  // }

  H5Gclose(meta);
  H5Gclose(channels);
  H5Fclose(root);

  return 1;
};

std::string getHashFromFieldData(vtkFieldData* fieldData){
  const size_t nArrays = fieldData->GetNumberOfArrays();
  std::vector<std::string> arrays(nArrays);
  for(size_t a=0; a<nArrays; a++){
    arrays[a] = std::string(fieldData->GetAbstractArray(a)->GetName());
  }
  std::sort(arrays.begin(),arrays.end());

  std::string hash_string = "";
  for(size_t a=0; a<nArrays; a++){
    auto array = fieldData->GetAbstractArray(arrays[a].data());
    const size_t nTuples = array->GetNumberOfTuples();
    const size_t nComponents = array->GetNumberOfComponents();
    const size_t nValues = nTuples*nComponents;
    for(size_t i=0; i<nValues; i++){
      hash_string += array->GetVariantValue(i).ToString();
    }
  }
  return std::to_string(std::hash<std::string>{}(hash_string));
};

int ensureDirectoryExists(const std::string &path) {
  auto directory = vtkSmartPointer<vtkDirectory>::New();
  if(directory->Open(path.data()) == 1 || vtkDirectory::MakeDirectory(path.data()) == 1)
    return 1;
  else
    return 0;
};

int validateOutputDirectoryPath(const std::string &path) {
  if(path.length() < 4 || path.substr(path.length() - 4, 4).compare(".cdb")!= 0) {
    CinemaAlgorithm::printErr("Output directory must have .cdb suffix");
    return 0;
  }
  return 1;
}

int getHeader(std::map<std::string,int>& header, const std::string path){
  const size_t MAX_NAME_LENGTH = 100;
  char obj_name[MAX_NAME_LENGTH];
  int h=0;
  const hid_t root = H5Fopen(path.data(), H5F_ACC_RDONLY, H5P_DEFAULT);
  if (root < 0) {
    fprintf(stderr, "Failed to open file\n");
    return 0;
  }

  const hid_t meta = H5Gopen(root, "/meta", H5P_DEFAULT);
  if (meta < 0) {
    fprintf(stderr, "Failed to open group\n");
    H5Fclose(root);
    return 0;
  }

  hsize_t nMeta;
  H5Gget_num_objs(meta,&nMeta);
  for(hsize_t m=0; m<nMeta; m++){

    // const auto name = meta.getObjnameByIdx(m);
    H5Gget_objname_by_idx(meta, m, obj_name, MAX_NAME_LENGTH);
    const std::string name(obj_name);

    const hid_t dataset = H5Dopen(root, ("/meta/"+name).data(), H5P_DEFAULT);
    const hid_t space = H5Dget_space(dataset);
    const int rank = H5Sget_simple_extent_ndims(space);

    hsize_t dims[3];
    H5Sget_simple_extent_dims(space, dims, NULL);

    if(dims[0]!=1 || name.find("Cam") != std::string::npos)
      continue;

    header.emplace(name,h++);
  }

  return 1;
}

int getValues(std::vector<std::string>& row, const std::map<std::string,int>& header, const std::string path){

  const size_t MAX_NAME_LENGTH = 100;
  char obj_name[MAX_NAME_LENGTH];
  int h=0;
  const hid_t root = H5Fopen(path.data(), H5F_ACC_RDONLY, H5P_DEFAULT);
  if (root < 0) {
    fprintf(stderr, "Failed to open file\n");
    return 0;
  }
  const hid_t meta = H5Gopen(root, "/meta", H5P_DEFAULT);
  if (meta < 0) {
    fprintf(stderr, "Failed to open group\n");
    H5Fclose(root);
    return 0;
  }

  for(const auto& h: header){
    const hid_t dataset = H5Dopen(root, ("/meta/"+h.first).data(), H5P_DEFAULT);
    float buf;
    H5Dread(dataset, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, &buf);
    std::stringstream value;
    value<<buf;
    row.push_back(value.str());
  }

  return 1;
}

int CinemaWriter::CreateDataCSV() const {
  if(!validateOutputDirectoryPath(this->OutputDirectory)) return 0;

  vtkNew<vtkDirectory> dir;
  dir->Open(this->OutputDirectory.data());

  const size_t nFiles = dir->GetNumberOfFiles();
  size_t nH5Files = 0;

  // header
  std::map<std::string,int> header;
  for(size_t f=0; f<nFiles; f++){
    const auto path = std::string(dir->GetFile(f));
    if(path.length() < 3 || path.substr(path.length() - 3, 3).compare(".h5")!= 0)
      continue;

    nH5Files++;
    if(!header.size())
      if(!getHeader(header,this->OutputDirectory+'/'+path)) return 0;
  }

  // values
  std::vector<std::vector<std::string>> values(nH5Files);
  for(size_t f=0, h5f=0; f<nFiles; f++){
    const auto path = std::string(dir->GetFile(f));
    if(path.length() < 3 || path.substr(path.length() - 3, 3).compare(".h5")!= 0)
      continue;

    auto& row = values[h5f++];
    if(!getValues(row,header,this->OutputDirectory+'/'+path)) return 0;
    row.push_back(path);
  }

  // write file
  {
    const std::string csvPath = this->OutputDirectory+"/data.csv";
    std::ofstream csvFile;
    csvFile.open(csvPath.data());
    if(!csvFile.is_open()) {
      CinemaAlgorithm::printErr("Unable to create 'data.csv' file.");
      return 0;
    }
    for(const auto& h: header)
      csvFile << h.first << ",";
    csvFile << "FILE\n";

    for(const auto& row: values){
      std::string row_;
      for(const auto& v: row)
        row_ += v + ",";
      row_.pop_back();
      csvFile << row_ << "\n";
    }
    csvFile.close();
  }

  return 1;
};

int CinemaWriter::DeleteDatabase() const {
  if(!validateOutputDirectoryPath(this->OutputDirectory)) return 0;
  return vtkDirectory::DeleteDirectory(this->OutputDirectory.data());
};

int CinemaWriter::RequestData(vtkInformation *request,
                 vtkInformationVector **inputVector,
                 vtkInformationVector *outputVector){

  auto input = vtkDataObject::GetData(inputVector[0]);

  vtkNew<vtkMultiBlockDataSet> inputAsMB;
  if(input->IsA("vtkMultiBlockDataSet")){
    inputAsMB->ShallowCopy(input);
  } else if(input->IsA("vtkImageData")) {
    inputAsMB->SetBlock(0,input);
  } else {
    CinemaAlgorithm::printErr("CinemaWriter only processes vtkMultiBlockDataSets that contain vtkImageData");
    return 0;
  }

  if(!validateOutputDirectoryPath(this->OutputDirectory)) return 0;
  if(!ensureDirectoryExists(this->OutputDirectory)) return 0;

  vtkNew<vtkPNGWriter> pngWriter;
  pngWriter->SetCompressionLevel(this->CompressionLevel);

  const size_t nImages = inputAsMB->GetNumberOfBlocks();
  this->printMsg("# Writer ("+std::to_string(nImages)+" images)");
  for(size_t i=0; i<nImages; i++){
    auto image = vtkImageData::SafeDownCast(inputAsMB->GetBlock(i));
    if(!image){
      CinemaAlgorithm::printErr("Writer only processes vtkMultiBlockDataSets that contain vtkImageData");
      continue;
    }

    const std::string image_hash = getHashFromFieldData(image->GetFieldData());

    if(this->Format==0){
      const std::string path = this->OutputDirectory + "/" + image_hash + ".h5";
      if(!writeImage(image,path,this->CompressionLevel))return 0;
    } else {
      const std::string path = this->OutputDirectory + "/" + image_hash + ".png";
      pngWriter->SetInputData(image);
      pngWriter->SetFileName(path.c_str());
      pngWriter->Write();
    }
  }

  auto output = vtkDataObject::GetData(outputVector);
  output->ShallowCopy(input);

  return 1;
};
