//
// <copyright file="MLFDataDeserializer.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//

#include "stdafx.h"
#include "MLFDataDeserializer.h"
#include "ConfigHelper.h"
#include "htkfeatio.h"
#include "msra_mgram.h"

namespace Microsoft { namespace MSR { namespace CNTK {

    MLFDataDeserializer::MLFDataDeserializer(const ConfigParameters& label, size_t elementSize, const HTKDataDeserializer* featureDeserializer, bool frameMode, const std::wstring& name)
        : m_mlfPaths(std::move(ConfigHelper::GetMlfPaths(label)))
        , m_elementSize(elementSize)
        , m_featureDeserializer(featureDeserializer)
        , m_frameMode(frameMode)
        , m_name(name)
    {
        ConfigHelper::CheckLabelType(label);

        m_dimension = ConfigHelper::GetLabelDimension(label);
        m_layout = std::make_shared<ImageLayout>(std::move(std::vector<size_t> { m_dimension }));

        m_stateListPath = label(L"labelMappingFile", L"");

        // TODO: currently assumes all Mlfs will have same root name (key)
        // restrict MLF reader to these files--will make stuff much faster without having to use shortened input files

        // get labels
        const double htktimetoframe = 100000.0; // default is 10ms

        const msra::lm::CSymbolSet* wordTable = nullptr;
        std::map<string, size_t>* symbolTable = nullptr;

        msra::asr::htkmlfreader<msra::asr::htkmlfentry, msra::lattices::lattice::htkmlfwordsequence> labels
            (m_mlfPaths, std::set<wstring>(), m_stateListPath, wordTable, symbolTable, htktimetoframe);

        // Make sure 'msra::asr::htkmlfreader' type has a move constructor
        static_assert(
            std::is_move_constructible <
            msra::asr::htkmlfreader < msra::asr::htkmlfentry,
            msra::lattices::lattice::htkmlfwordsequence >> ::value,
            "Type 'msra::asr::htkmlfreader' should be move constructible!");

        MLFUtterance description;
        description.id = 0;
        description.isValid = true; // right now we throw for invalid sequences
        // TODO .chunk, .key

        size_t totalFrames = 0;
        // Have to iterate in the same order as utterances inside the HTK data de-serializer to be aligned.
        for(const auto& u : featureDeserializer->GetUtterances())
        {
            wstring key = u.utterance.key();

            // todo check that actually exists.
            auto l = labels.find(key);
            const auto & labseq = l->second;

            description.sequenceStart = m_classIds.size(); // TODO
            description.isValid = true;
            size_t numofframes = 0;
            description.id++;

            foreach_index(i, labseq)
            {
                // TODO Why will these yield a run-time error as opposed to making the utterance invalid?
                const auto & e = labseq[i];
                if ((i == 0 && e.firstframe != 0) ||
                    (i > 0 && labseq[i - 1].firstframe + labseq[i - 1].numframes != e.firstframe))
                {
                    RuntimeError("minibatchutterancesource: labels not in consecutive order MLF in label set: %ls", l->first.c_str());
                }

                if (e.classid >= m_dimension)
                {
                    RuntimeError("minibatchutterancesource: class id %llu exceeds model output dimension %llu in file",
                        e.classid, m_dimension);
                }

                if (e.classid != static_cast<msra::dbn::CLASSIDTYPE>(e.classid))
                {
                    RuntimeError("CLASSIDTYPE has too few bits");
                }

                for (size_t t = e.firstframe; t < e.firstframe + e.numframes; t++)
                {
                    m_classIds.push_back(e.classid);
                    numofframes++;
                }
            }

            description.numberOfSamples = numofframes;
            totalFrames += numofframes;
            m_utterances.push_back(description);
        }

        if (m_frameMode)
        {
            m_frames.reserve(totalFrames);
        }
        else
        {
            m_sequences.reserve(m_utterances.size());
        }

        foreach_index(i, m_utterances)
        {
            if (m_frameMode)
            {
                for (size_t k = 0; k < m_utterances[i].numberOfSamples; ++k)
                {
                    MLFFrame f;
                    f.id = m_frames.size();
                    f.chunkId = 0;
                    f.numberOfSamples = 1;
                    f.index = m_utterances[i].sequenceStart + k;
                    assert(m_utterances[i].isValid); // TODO
                    f.isValid = m_utterances[i].isValid;
                    m_frames.push_back(f);
                    m_sequences.push_back(&m_frames[f.id]);
                }
            }
            else
            {
                assert(false);
                m_sequences.push_back(&m_utterances[i]);
            }
        }
    }

    void Microsoft::MSR::CNTK::MLFDataDeserializer::SetEpochConfiguration(const EpochConfiguration& /*config*/)
    {
        throw std::logic_error("The method or operation is not implemented.");
    }

    const TimelineP& Microsoft::MSR::CNTK::MLFDataDeserializer::GetSequenceDescriptions() const
    {
        return m_sequences;
    }

    std::vector<InputDescriptionPtr> MLFDataDeserializer::GetInputs() const
    {
        InputDescriptionPtr input = std::make_shared<InputDescription>();
        input->id = 0;
        input->name = m_name;
        input->sampleLayout = std::make_shared<ImageLayout>(std::move(std::vector<size_t>{ m_dimension }));
        return std::vector<InputDescriptionPtr> { input };
    }

    std::vector<Sequence> MLFDataDeserializer::GetSequenceById(size_t id)
    {
        assert(m_frameMode);

        size_t label = m_classIds[m_frames[id].index];
        Sequence r;
        if (m_elementSize == sizeof(float))
        {
            float* tmp = new float[m_dimension];
            memset(tmp, 0, m_elementSize * m_dimension);
            tmp[label] = 1;
            r.data = tmp;
        }
        else
        {
            double* tmp = new double[m_dimension];
            memset(tmp, 0, m_elementSize * m_dimension);
            tmp[label] = 1;
            r.data = tmp;
        }

        r.numberOfSamples = m_sequences[id]->numberOfSamples;

        return std::vector<Sequence> { r };
    }

    bool MLFDataDeserializer::RequireChunk(size_t /*chunkIndex*/)
    {
        return false;
    }

    void MLFDataDeserializer::ReleaseChunk(size_t /*chunkIndex*/)
    {
    }

    const std::vector<MLFUtterance>& MLFDataDeserializer::GetUtterances() const
    {
        return m_utterances;
    }

}}}
