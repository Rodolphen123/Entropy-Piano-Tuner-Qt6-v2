/*****************************************************************************
 * Copyright 2016 Haye Hinrichsen, Christoph Wick
 *
 * This file is part of Entropy Piano Tuner.
 *
 * Entropy Piano Tuner is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *****************************************************************************/

#include "entropyevaluator.h"

#include <algorithm>

#include "core/system/log.h"
#include "core/math/mathtools.h"
#include "auditorypreprocessing.h"

namespace entropyminimizerv2
{

//-----------------------------------------------------------------------------
//                          Constructor / destructor
//-----------------------------------------------------------------------------

EntropyEvaluator::EntropyEvaluator(const Piano &piano) :
    mPiano(piano)
{}

EntropyEvaluator::~EntropyEvaluator() = default;

//-----------------------------------------------------------------------------
//                              prepare
//-----------------------------------------------------------------------------

bool EntropyEvaluator::prepare(const Parameters &params,
    const std::vector<Key::SpectrumType>* spectrumCalculate,
    const std::vector<Key::SpectrumType>* rawSpectra)
{
    const int N = mPiano.getKeyboard().getNumberOfKeys();

    // V2 (2.4): copie locale du Piano. Toutes les modifications (B
    // extrapolés, spectres prétraités) se font sur cette copie, jamais
    // sur mPiano. Ainsi les <spectrum> bruts restent intacts pour la
    // sauvegarde .ept.
    Piano localPiano(mPiano);

    // --- Cutoffs (identiques à ceux de EntropyMinimizer) ---
    const double highestFrequency =
        localPiano.getEqualTempFrequency(N - 1, 0, 440) * 1.13;
    mLowerCutoff = 100;
    mUpperCutoff = std::min(
        Key::NumberOfBins - 100,
        MathTools::roundToInteger(Key::FrequencyToRealIndex(highestFrequency)));

    AuditoryPreprocessing AP(localPiano);

    // V2 (2.4): vérification de cohérence. Si échec, on ne prépare rien
    // et on laisse mPrepared = false, pour que l'UI n'affiche pas de
    // valeurs aberrantes.
    if (!AP.checkDataConsistency())
    {
        LogW("EntropyEvaluator::prepare: données inconsistantes, abandon");
        return false;
    }

    // ----------------------------------------------------------------
    // H_commun : chaîne figée (normalize → SPLA → renormalize → mollifier)
    // Pas de cleanSpectrum, pas de improveHighFrequencyPeaks, pas de
    // cutLowFrequencies. scale = 1, maxBassWidthCents = 1e9 (pas de cap).
    // On travaille sur des copies locales des spectres, localPiano n'est
    // pas modifié.
    // ----------------------------------------------------------------
    {
        AP.initializeSPLAFilter();

        mCommunSpectra.resize(N);
        for (int k = 0; k < N; ++k)
        {
            if (rawSpectra && (rawSpectra->size() != static_cast<size_t>(N)
                || (*rawSpectra)[k].size() != Key::NumberOfBins))
            {
                LogW("EntropyEvaluator::prepare: spectre brut invalide (touche %d)", k);
                return false;
            }
            Key::SpectrumType s = rawSpectra
                ? (*rawSpectra)[k] : localPiano.getKey(k).getSpectrum();
            AP.normalizeSpectrum(s);
            AP.convertToSPLA(s);
            AP.normalizeSpectrum(s);
            AP.applyMollifier(s, 1.0, 1e9);
            mCommunSpectra[k] = std::move(s);
        }
        LogI("EntropyEvaluator: H_commun préparé (%d touches, version %d)",
             N, COMMUN_CHAIN_VERSION);
    }

    // ----------------------------------------------------------------
    // H_moteur : réutiliser le résultat pré-mollification produit par le
    // moteur principal. Cela évite de retraiter une seconde fois toutes les
    // touches pour l'évaluation 2.4.
    // ----------------------------------------------------------------
    {
        if (spectrumCalculate && spectrumCalculate->size() != static_cast<size_t>(N))
        {
            LogW("EntropyEvaluator::prepare: nombre de spectres moteur invalide");
            return false;
        }
        if (spectrumCalculate)
        {
            for (int k = 0; k < N; ++k)
            {
                if ((*spectrumCalculate)[k].size() != Key::NumberOfBins)
                {
                    LogW("EntropyEvaluator::prepare: taille de spectrumcalculate invalide (touche %d)", k);
                    return false;
                }
                localPiano.getKey(k).getSpectrum() = (*spectrumCalculate)[k];
                AP.applyMollifier(localPiano.getKey(k), params.mollifierScale, params.maxBassWidth);
            }
        }
        else
        {
            // Compatibilité pour les appels qui ne fournissent pas encore le cache.
            AP.setHighFrequencyPeaksPolicy(params.highFrequencyPeaksPolicy);
            AP.setInharmonicityPolicy(params.inharmonicityPolicy);
            AP.setBreakKeyIndex(params.breakKeyIndex);
            AP.extrapolateInharmonicity();
            for (int k = 0; k < N; ++k) AP.normalizeSpectrum(localPiano.getKey(k));
            for (int k = 0; k < N; ++k) AP.cleanSpectrum(localPiano.getKey(k));
            for (int k = 0; k < N; ++k) AP.cutLowFrequencies(localPiano.getKey(k));
            AP.initializeSPLAFilter();
            for (int k = 0; k < N; ++k) AP.convertToSPLA(localPiano.getKey(k).getSpectrum());
            AP.improveHighFrequencyPeaks();
            for (int k = 0; k < N; ++k)
                AP.applyMollifier(localPiano.getKey(k), params.mollifierScale, params.maxBassWidth);
        }

        mMoteurSpectra.resize(N);
        for (int k = 0; k < N; ++k)
            mMoteurSpectra[k] = localPiano.getKey(k).getSpectrum();

        LogI("EntropyEvaluator: H_moteur préparé (%d touches)", N);
    }

    mPrepared = true;
    return true;
}

} // namespace entropyminimizerv2
