/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "MlpPolicy.h"

#include <AzCore/Console/ILogger.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/std/algorithm.h>

namespace NeuralBots
{
    bool MlpPolicy::LoadFromFile(const AZStd::string& path)
    {
        Clear();

        AZ::IO::FileIOBase* fileIo = AZ::IO::FileIOBase::GetInstance();
        if (!fileIo)
        {
            AZLOG_ERROR("MlpPolicy: no FileIO instance");
            return false;
        }

        AZ::IO::HandleType handle = AZ::IO::InvalidHandle;
        if (!fileIo->Open(path.c_str(), AZ::IO::OpenMode::ModeRead, handle))
        {
            AZLOG_ERROR("MlpPolicy: cannot open '%s'", path.c_str());
            return false;
        }

        AZ::u64 size = 0;
        fileIo->Size(handle, size);
        AZStd::vector<char> buffer(size + 1, '\0');
        fileIo->Read(handle, buffer.data(), size);
        fileIo->Close(handle);

        rapidjson::Document doc;
        doc.Parse(buffer.data());
        if (doc.HasParseError() || !doc.IsObject())
        {
            AZLOG_ERROR("MlpPolicy: '%s' is not valid JSON", path.c_str());
            return false;
        }

        if (!doc.HasMember("format") || !doc["format"].IsString() ||
            azstricmp(doc["format"].GetString(), "mlp-1") != 0)
        {
            AZLOG_ERROR("MlpPolicy: '%s' has unsupported format (expected \"mlp-1\")", path.c_str());
            return false;
        }

        if (!doc.HasMember("layers") || !doc["layers"].IsArray())
        {
            AZLOG_ERROR("MlpPolicy: '%s' has no layers array", path.c_str());
            return false;
        }

        size_t previousWidth = 0;
        for (const auto& layerValue : doc["layers"].GetArray())
        {
            if (!layerValue.IsObject() || !layerValue.HasMember("weights") || !layerValue["weights"].IsArray() ||
                !layerValue.HasMember("biases") || !layerValue["biases"].IsArray())
            {
                AZLOG_ERROR("MlpPolicy: '%s' has a malformed layer", path.c_str());
                Clear();
                return false;
            }

            Layer layer;
            const auto& rows = layerValue["weights"].GetArray();
            layer.m_outputWidth = rows.Size();
            if (layer.m_outputWidth == 0 || !rows[0].IsArray())
            {
                AZLOG_ERROR("MlpPolicy: '%s' has an empty weights matrix", path.c_str());
                Clear();
                return false;
            }
            layer.m_inputWidth = rows[0].GetArray().Size();

            if (previousWidth != 0 && layer.m_inputWidth != previousWidth)
            {
                AZLOG_ERROR("MlpPolicy: '%s' layer widths do not chain", path.c_str());
                Clear();
                return false;
            }
            previousWidth = layer.m_outputWidth;

            layer.m_weights.reserve(layer.m_outputWidth * layer.m_inputWidth);
            for (const auto& row : rows)
            {
                if (!row.IsArray() || row.GetArray().Size() != layer.m_inputWidth)
                {
                    AZLOG_ERROR("MlpPolicy: '%s' has a ragged weights matrix", path.c_str());
                    Clear();
                    return false;
                }
                for (const auto& weight : row.GetArray())
                {
                    layer.m_weights.push_back(weight.GetFloat());
                }
            }

            const auto& biases = layerValue["biases"].GetArray();
            if (biases.Size() != layer.m_outputWidth)
            {
                AZLOG_ERROR("MlpPolicy: '%s' bias count does not match", path.c_str());
                Clear();
                return false;
            }
            layer.m_biases.reserve(layer.m_outputWidth);
            for (const auto& bias : biases)
            {
                layer.m_biases.push_back(bias.GetFloat());
            }

            layer.m_activation = Activation::None;
            if (layerValue.HasMember("activation") && layerValue["activation"].IsString())
            {
                const char* name = layerValue["activation"].GetString();
                if (azstricmp(name, "relu") == 0)
                {
                    layer.m_activation = Activation::Relu;
                }
                else if (azstricmp(name, "tanh") == 0)
                {
                    layer.m_activation = Activation::Tanh;
                }
                else if (azstricmp(name, "sigmoid") == 0)
                {
                    layer.m_activation = Activation::Sigmoid;
                }
                else if (azstricmp(name, "leaky_relu") == 0)
                {
                    layer.m_activation = Activation::LeakyRelu;
                }
            }

            m_layers.push_back(AZStd::move(layer));
        }

        if (m_layers.empty())
        {
            AZLOG_ERROR("MlpPolicy: '%s' contains no layers", path.c_str());
            return false;
        }

        return true;
    }

    size_t MlpPolicy::InputWidth() const
    {
        return m_layers.empty() ? 0 : m_layers.front().m_inputWidth;
    }

    size_t MlpPolicy::OutputWidth() const
    {
        return m_layers.empty() ? 0 : m_layers.back().m_outputWidth;
    }

    bool MlpPolicy::Evaluate(const AZStd::vector<float>& input, AZStd::vector<float>& output) const
    {
        if (m_layers.empty() || input.size() != InputWidth())
        {
            return false;
        }

        AZStd::vector<float> current = input;
        AZStd::vector<float> next;
        for (const Layer& layer : m_layers)
        {
            next.assign(layer.m_outputWidth, 0.0f);
            for (size_t out = 0; out < layer.m_outputWidth; ++out)
            {
                float sum = layer.m_biases[out];
                const float* row = &layer.m_weights[out * layer.m_inputWidth];
                for (size_t in = 0; in < layer.m_inputWidth; ++in)
                {
                    sum += row[in] * current[in];
                }
                switch (layer.m_activation)
                {
                case Activation::Relu:
                    sum = AZStd::max(sum, 0.0f);
                    break;
                case Activation::Tanh:
                    sum = tanhf(sum);
                    break;
                case Activation::Sigmoid:
                    sum = 1.0f / (1.0f + expf(-sum));
                    break;
                case Activation::LeakyRelu:
                    sum = sum >= 0.0f ? sum : 0.01f * sum;
                    break;
                case Activation::None:
                    break;
                }
                next[out] = sum;
            }
            current.swap(next);
        }

        output = AZStd::move(current);
        return true;
    }
} // namespace NeuralBots
