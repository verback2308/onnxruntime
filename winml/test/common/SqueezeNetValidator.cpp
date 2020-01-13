#include "SqueezeNetValidator.h"
#include "protobufHelpers.h"
#include "fileHelpers.h"
#include "core/common/common.h"
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>
// using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::AI::MachineLearning;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;

namespace WinML::Engine::Test{

#define MAX_PROFILING_LOOP 100


static void BindImage(
    LearningModelBinding binding,
    const wchar_t* name,
    const wchar_t* fullImagePath,
    bool bindAsInspectable = false)
{
    auto imagefile = StorageFile::GetFileFromPathAsync(fullImagePath).get();
    auto stream = imagefile.OpenAsync(FileAccessMode::Read).get();
    auto decoder = BitmapDecoder::CreateAsync(stream).get();
    auto softwareBitmap = decoder.GetSoftwareBitmapAsync().get();
    auto frame = VideoFrame::CreateWithSoftwareBitmap(softwareBitmap);

    if (bindAsInspectable)
    {
        WINML_EXPECT_NO_THROW(binding.Bind(name, frame));
    }
    else
    {
        auto imagetensor = ImageFeatureValue::CreateFromVideoFrame(frame);
        WINML_EXPECT_NO_THROW(binding.Bind(name, imagetensor));
    }
}

static void BindTensor(
    LearningModelBinding binding,
    const wchar_t* name,
    ITensor inputTensor,
    bool bindAsInspectable = false)
{
    WINML_EXPECT_TRUE(inputTensor != nullptr);

    if (bindAsInspectable)
    {
        WINML_EXPECT_NO_THROW(binding.Bind(name, inputTensor.as<TensorFloat>().GetAsVectorView()));
    }
    else
    {
        WINML_EXPECT_NO_THROW(binding.Bind(name, inputTensor));
    }
}

template <typename T>
ITensor BindOutput(
    OutputBindingStrategy strategy,
    LearningModelBinding binding,
    const wchar_t* name,
    const IVectorView<int64_t> shape = nullptr
)
{
    ITensor outputTensor = nullptr;
    switch (strategy)
    {
    case OutputBindingStrategy::Bound:
        outputTensor = T::Create(shape);
        WINML_EXPECT_NO_THROW(binding.Bind(name, outputTensor));
        break;
    case OutputBindingStrategy::Empty:
        outputTensor = T::Create();
        WINML_EXPECT_NO_THROW(binding.Bind(name, outputTensor));
        break;
    case OutputBindingStrategy::Unbound:
        __fallthrough;
    default:
        break;
    }

    return outputTensor;
}

ImageFeatureValue BindImageOutput(
    OutputBindingStrategy strategy,
    LearningModelBinding binding,
    const wchar_t* name
)
{
    ImageFeatureValue outputTensor = nullptr;
    switch (strategy)
    {
    case OutputBindingStrategy::Bound:
    {
        SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, 720, 720);
        VideoFrame frame = VideoFrame::CreateWithSoftwareBitmap(bitmap);
        outputTensor = ImageFeatureValue::CreateFromVideoFrame(frame);
        WINML_EXPECT_NO_THROW(binding.Bind(name, outputTensor));
        break;
    }
    case OutputBindingStrategy::Unbound:
        __fallthrough;
    }

    return outputTensor;
}


void ModelValidator::FnsCandy16(
    std::string instance,
    LearningModelDeviceKind deviceKind,
    OutputBindingStrategy outputBindingStrategy,
    bool bindInputsAsIInspectable,
    float dataTolerance)
{
    ORT_UNUSED_PARAMETER(dataTolerance);
    // file name strings
    static wchar_t* modelFileName = L"winmlperf_coreml_FNS-Candy_prerelease_fp16.onnx";
    static wchar_t* inputDataImageFileName = L"fish_720.png";
    static wchar_t* outputDataFileName = L"output.png";
    static wchar_t* inputBindingName = L"inputImage";
    static const wchar_t* outputDataBindingName = L"outputImage";

    auto modulePath = FileHelpers::GetModulePath();
    auto fullModelPath = modulePath + modelFileName;
    auto outputFileName = modulePath + outputDataFileName;

    // WinML model creation
    LearningModel model = nullptr;
    WINML_EXPECT_NO_THROW(model = LearningModel::LoadFromFilePath(fullModelPath));

    LearningModelSession modelSession = nullptr;
    WINML_EXPECT_NO_THROW(modelSession = LearningModelSession(model, LearningModelDevice(deviceKind)));

    LearningModelBinding modelBinding(modelSession);
    auto fullImagePath = modulePath + inputDataImageFileName;
    BindImage(modelBinding, inputBindingName, fullImagePath.c_str(), bindInputsAsIInspectable);

    // create the tensor for the actual output
    auto output = model.OutputFeatures().First().Current();
    WINML_EXPECT_TRUE(output.Kind() == LearningModelFeatureKind::Tensor);

    auto shape = winrt::single_threaded_vector(std::vector<int64_t> {1, 1});
    auto outputTensor = BindImageOutput(outputBindingStrategy, modelBinding, outputDataBindingName);

    // Evaluate the model
    std::cout << "Calling EvaluateSync on instance" << instance << "\n";
    LearningModelEvaluationResult result = nullptr;
    WINML_EXPECT_NO_THROW(result = modelSession.Evaluate(modelBinding, {}));

    // Get results
    if (outputBindingStrategy == OutputBindingStrategy::Unbound)
    {
        // When output binding strategy is unbound, the output tensor was not set on bind.
        // Therefore, we need to retrieve it from the LearnignModelEvaluationResult
        // TODO: is this right? outputTensorT is unused...
        /*auto outputTensorT = */result.Outputs().Lookup(outputDataBindingName).as<TensorFloat16Bit>();
    }
    else
    {
        WINML_EXPECT_EQUAL(result.Outputs().Lookup(outputDataBindingName), outputTensor);

        auto softwareBitmap = outputTensor.VideoFrame().SoftwareBitmap();

        auto folder = StorageFolder::GetFolderFromPathAsync(modulePath.c_str()).get();
        auto imagefile = folder.CreateFileAsync(outputDataFileName, CreationCollisionOption::ReplaceExisting).get();
        auto stream = imagefile.OpenAsync(FileAccessMode::ReadWrite).get();
        auto encoder = BitmapEncoder::CreateAsync(BitmapEncoder::JpegEncoderId(), stream).get();
        encoder.SetSoftwareBitmap(softwareBitmap);
        encoder.FlushAsync();

    }
}

void ModelValidator::SqueezeNet(
    std::string instance,
    LearningModelDeviceKind deviceKind,
    float dataTolerance,
    bool bindAsImage,
    OutputBindingStrategy outputBindingStrategy,
    bool bindInputsAsIInspectable)
{
    // file name strings
    static wchar_t* modelFileName = L"model.onnx";
    static wchar_t* inputDataFileName = L"test_data_0_input.pb";
    static wchar_t* outputDataFileName = L"test_data_0_output.pb";
    static wchar_t* inputBindingName = L"data_0";
    static wchar_t* inputDataImageFileName = L"kitten_224.png";
    static const wchar_t* outputDataBindingName = L"softmaxout_1";

    auto modulePath = FileHelpers::GetModulePath();
    auto fullModelPath = modulePath + modelFileName;
    auto outputFileName = modulePath + outputDataFileName;
    
    // WinML model creation
    LearningModel model = nullptr;
    WINML_EXPECT_NO_THROW(model = LearningModel::LoadFromFilePath(fullModelPath));
    
    LearningModelSession modelSession = nullptr;
    WINML_EXPECT_NO_THROW(modelSession = LearningModelSession(model, LearningModelDevice(deviceKind)));

    LearningModelBinding modelBinding(modelSession);

    if (bindAsImage)
    {
        std::wstring fullImagePath = modulePath + inputDataImageFileName;
        BindImage(modelBinding, inputBindingName, fullImagePath.c_str(), bindInputsAsIInspectable);
    }
    else
    {
        auto inputDataPath = modulePath + inputDataFileName;
        auto inputTensor = ProtobufHelpers::LoadTensorFromProtobufFile(inputDataPath, false);
        BindTensor(modelBinding, inputBindingName, inputTensor, bindInputsAsIInspectable);
    }

    // load up the expected output
    auto expectedResultsTensor = ProtobufHelpers::LoadTensorFromProtobufFile(outputFileName, false);
    WINML_EXPECT_TRUE(expectedResultsTensor != nullptr);

    // create the tensor for the actual output
    auto output = model.OutputFeatures().First().Current();
    WINML_EXPECT_TRUE(output.Kind() == LearningModelFeatureKind::Tensor);

    auto outputTensor = BindOutput<TensorFloat>(
        outputBindingStrategy, modelBinding, outputDataBindingName, expectedResultsTensor.Shape());

    // Evaluate the model
    std::cout << "Calling EvaluateSync on instance" << instance << "\n";
    LearningModelEvaluationResult result = nullptr;
    WINML_EXPECT_NO_THROW(result = modelSession.Evaluate(modelBinding, {}));

    // Get results
    if (outputBindingStrategy == OutputBindingStrategy::Unbound)
    {
        // When output binding strategy is unbound, the output tensor was not set on bind.
        // Therefore, we need to retrieve it from the LearnignModelEvaluationResult
        outputTensor = result.Outputs().Lookup(outputDataBindingName).as<ITensor>();
    }
    else
    {
        WINML_EXPECT_EQUAL(result.Outputs().Lookup(outputDataBindingName), outputTensor);
    }

    auto outDataExpected = expectedResultsTensor.as<TensorFloat>().GetAsVectorView();
    auto outDataActual = outputTensor.as<TensorFloat>().GetAsVectorView();

    WINML_EXPECT_TRUE(outDataActual.Size() == outDataExpected.Size());
    for (uint32_t i = 0; i < outDataActual.Size(); i++)
    {
        float delta = std::abs(outDataActual.GetAt(i) - outDataExpected.GetAt(i));
        if (delta > dataTolerance)
        {
          std::stringstream ss;
          ss << "EXPECTED: " << outDataExpected.GetAt(i) << " , ACTUAL: " << outDataActual.GetAt(i)
                << "instance " << instance << ", element " << i;
          WINML_LOG_ERROR(ss.str().c_str());
        }
    }
}
}
