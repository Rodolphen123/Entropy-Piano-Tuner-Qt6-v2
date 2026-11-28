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

//=============================================================================
//                          Entropy evaluator (V2 2.4)
//=============================================================================
//
// Évalue H_rec, H_courbe et ΔH dans deux référentiels :
//   - H_moteur : chaîne de prétraitement complète de v2.
//   - H_commun : chaîne figée (normalize → SPLA → renormalize → mollifier
//                à scale=1, sans cap), indépendante du moteur et du modèle
//                d'inharmonicité.
//
// Peut être utilisé pendant le calcul (par EntropyMinimizer) ou hors calcul
// (par CalculationProgressGroup), pour répondre au bouton « Évaluer » et aux
// retouches manuelles (MSG_CHANGE_TUNING_CURVE).
//
// La classe garde en mémoire les spectres prétraités des deux référentiels,
// pour répondre en < 1 ms.

#ifndef ENTROPYEVALUATOR_H
#define ENTROPYEVALUATOR_H

#include "prerequisites.h"

#include <vector>

#include "core/piano/piano.h"
#include "core/piano/key.h"

namespace entropyminimizerv2
{

// passer par un message
class  EntropyEvaluator
{
public:
    // V2 (2.4): version de la chaîne H_commun. À incrémenter à chaque
    // modification de la chaîne figée (normalize/SPLA/renormalize/mollifier).
    // Enregistrée dans le journal (3.10).
    static constexpr int COMMUN_CHAIN_VERSION = 1;

    // Paramètres de prétraitement (transmis depuis le XML de l'algorithme).
    struct Parameters
    {
        int    highFrequencyPeaksPolicy = 2;   // 0=legacy, 1=off, 2=complement
        int    inharmonicityPolicy      = 1;   // 0=replace, 1=keep
        int    breakKeyIndex            = 0;   // 0=auto
        double mollifierScale           = 1.0;
        double maxBassWidth             = 50.0;
    };

    explicit EntropyEvaluator(const Piano &piano);
    ~EntropyEvaluator();

    // Prépare les spectres des deux référentiels.
    // Travaille sur une COPIE LOCALE du Piano : mPiano n'est jamais modifié,
    // donc les <spectrum> bruts restent intacts pour la sauvegarde .ept.
    // Retourne false si les données sont inconsistantes (touches non
    // enregistrées, spectres vides, etc.).
    bool prepare(const Parameters &params,
                 const std::vector<Key::SpectrumType>* spectrumCalculate = nullptr,
                 const std::vector<Key::SpectrumType>* rawSpectra = nullptr);

    bool isPrepared() const { return mPrepared; }

    // TODO (étape 2) : méthodes d'évaluation
    //   double evaluateH_moteur(const std::vector<double>& shift) const;
    //   double evaluateH_commun(const std::vector<double>& shift) const;
    //   double H_rec_moteur() const;
    //   double H_rec_commun() const;

    // V2 (2.4): accès aux spectres prétraités, pour que EntropyMinimizer
    // puisse construire ses accumulateurs.
    const std::vector<Key::SpectrumType>& getMoteurSpectra() const { return mMoteurSpectra; }
    const std::vector<Key::SpectrumType>& getCommunSpectra() const { return mCommunSpectra; }

private:
    const Piano &mPiano;

    // Spectres prétraités, un par touche.
    std::vector<Key::SpectrumType> mMoteurSpectra;   // chaîne v2 complète
    std::vector<Key::SpectrumType> mCommunSpectra;   // chaîne figée 2.4

    // Cutoffs (mêmes que EntropyMinimizer, pour l'accumulateur).
    int mLowerCutoff = 100;
    int mUpperCutoff = 0;

    bool mPrepared = false;
};

} // namespace entropyminimizerv2

#endif // ENTROPYEVALUATOR_H
