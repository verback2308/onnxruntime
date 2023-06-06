﻿using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Microsoft.ML.OnnxRuntime.Tensors;

namespace Microsoft.ML.OnnxRuntime.InferenceSample
{
    public class InferenceSampleApi : IDisposable
    {
        public InferenceSampleApi()
        {
            _model = LoadModelFromEmbeddedResource("TestData.squeezenet.onnx");

            // this is the data for only one input tensor for this model
            var inputData = LoadTensorFromEmbeddedResource("TestData.bench.in");
            var mem = new Memory<float>(inputData);
            _inputPin = mem.Pin();

            IntPtr dataPtr;
            unsafe
            {
                dataPtr = (IntPtr)(_inputPin).Pointer;
            }

            // create default session with default session options
            // Creating an InferenceSession and loading the model is an expensive operation, so generally you would
            // do this once. InferenceSession.Run can be called multiple times, and concurrently.
            CreateInferenceSession();

            // setup sample input data
            var inputMeta = _inferenceSession.InputMetadata;
            _inputData = new List<OrtValue>(inputMeta.Count);
            _orderedInputNames = new List<string>(inputMeta.Count);

            foreach (var name in inputMeta.Keys)
            {
                // We create an OrtValue in this case over the buffer of potentially different shapes.
                // It is Okay as long as the specified shape does not exceed the actual length of the buffer
                var shape = Array.ConvertAll<int, long>(inputMeta[name].Dimensions, d => d);
                var shapeLen = shape.Aggregate(1L, (a, v) => a * v);
                Debug.Assert(shapeLen <= inputData.LongLength);
                var bufferSize = shapeLen * sizeof(float);

                var ortValue = OrtValue.CreateTensorValueWithData(OrtMemoryInfo.DefaultInstance, TensorElementType.Float,
                    shape, dataPtr, bufferSize);
                _inputData.Add(ortValue);

                _orderedInputNames.Add(name);
            }
        }

        public void CreateInferenceSession(SessionOptions options = null)
        {
            // Optional : Create session options and set any relevant values.
            // If an additional execution provider is needed it should be added to the SessionOptions prior to
            // creating the InferenceSession. The CPU Execution Provider is always added by default.
            if (options == null)
            {
                options = new SessionOptions { LogId = "Sample" };
            }

            _inferenceSession = new InferenceSession(_model, options);
        }

        public void Execute()
        {
            // Run the inference
            // 'results' is an IDisposableReadOnlyCollection<OrtValue> container
            using (var results = _inferenceSession.Run(null, _orderedInputNames, _inputData, _inferenceSession.OutputNames))
            {
                // dump the results
                for (int i = 0; i < results.Count; ++i)
                {
                    var name = _inferenceSession.OutputNames[i];
                    Console.WriteLine("Output for {0}", name);
                    // Use tensor to print out the information. ToArray() creates a copy of data,
                    // however, you can directly access native memory via returned Span<T>
                    var mem = new Memory<float>(results[i].GetTensorDataAsSpan<float>().ToArray());
                    var dt = new DenseTensor<float>(mem, _inferenceSession.OutputMetadata[name].Dimensions);
                    Console.WriteLine(dt.GetArrayString());
                }
            }
        }

        protected virtual void Dispose(bool disposing)
        {
            if (disposing && !_disposed)
            {
                _inferenceSession?.Dispose();
                _inferenceSession = null;

                if (_inputData != null)
                    foreach(var v in _inputData)
                    {
                        v?.Dispose();
                    }

                _inputPin.Dispose();

                _disposed = true;
            }
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        static float[] LoadTensorFromEmbeddedResource(string path)
        {
            var tensorData = new List<float>();
            var assembly = typeof(InferenceSampleApi).Assembly;

            using (var inputFile = 
                new StreamReader(assembly.GetManifestResourceStream($"{assembly.GetName().Name}.{path}")))
            {
                inputFile.ReadLine(); // skip the input name
                string[] dataStr = inputFile.ReadLine().Split(new char[] { ',', '[', ']' }, 
                                                              StringSplitOptions.RemoveEmptyEntries);
                for (int i = 0; i < dataStr.Length; i++)
                {
                    tensorData.Add(Single.Parse(dataStr[i]));
                }
            }

            return tensorData.ToArray();
        }

        static byte[] LoadModelFromEmbeddedResource(string path)
        {
            var assembly = typeof(InferenceSampleApi).Assembly;
            byte[] model = null;

            using (Stream stream = assembly.GetManifestResourceStream($"{assembly.GetName().Name}.{path}"))
            {
                using (MemoryStream memoryStream = new MemoryStream())
                {
                    stream.CopyTo(memoryStream);
                    model = memoryStream.ToArray();
                }
            }

            return model;
        }

        private bool _disposed = false;
        private readonly byte[] _model;
        private readonly MemoryHandle _inputPin = default; // One time for inputs is Okay here.
        private readonly List<string> _orderedInputNames;
        private readonly List<OrtValue> _inputData;
        private InferenceSession _inferenceSession;
    }
}
