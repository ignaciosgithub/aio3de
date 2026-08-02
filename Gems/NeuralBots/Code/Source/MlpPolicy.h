/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace NeuralBots
{
    //! Minimal dense MLP inference. Loads the "mlp-1" JSON weights format
    //! exported by the AIBackbone gem (<model>.weights.json).
    class MlpPolicy
    {
    public:
        //! Loads weights from a JSON file. Returns false (and clears the
        //! policy) on any error; failure details go to the console.
        bool LoadFromFile(const AZStd::string& path);

        bool IsLoaded() const
        {
            return !m_layers.empty();
        }

        size_t InputWidth() const;
        size_t OutputWidth() const;

        //! Runs the network. Returns false if not loaded or the input width
        //! does not match.
        bool Evaluate(const AZStd::vector<float>& input, AZStd::vector<float>& output) const;

        void Clear()
        {
            m_layers.clear();
        }

    private:
        enum class Activation : AZ::u8
        {
            None,
            Relu,
            Tanh,
            Sigmoid,
            LeakyRelu
        };

        struct Layer
        {
            size_t m_inputWidth = 0;
            size_t m_outputWidth = 0;
            AZStd::vector<float> m_weights; //!< row-major [out][in]
            AZStd::vector<float> m_biases;
            Activation m_activation = Activation::None;
        };

        AZStd::vector<Layer> m_layers;
    };
} // namespace NeuralBots
