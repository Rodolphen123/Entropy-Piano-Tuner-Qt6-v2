/*****************************************************************************
 * Copyright 2016 Haye Hinrichsen, Christoph Wick
 *
 * This file is part of Entropy Piano Tuner.
 *
 * Entropy Piano Tuner is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Entropy Piano Tuner is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Entropy Piano Tuner. If not, see http://www.gnu.org/licenses/.
 *****************************************************************************/

//=============================================================================
//                           Auditory preprocessing
//=============================================================================

#ifndef AUDITORYPREPROCESSING_H
#define AUDITORYPREPROCESSING_H

#include "core/system/simplethreadhandler.h"
#include "core/system/log.h"
#include "core/piano/piano.h"

namespace entropyminimizerv2
{

////////////////////////////////////////////////////////////////////////
/// \brief Class of functions for auditory preprocessing
///
/// This class includes functions needed for the preparation of the
/// spectra. It is controlled by the CalculationManager.
////////////////////////////////////////////////////////////////////////

class AuditoryPreprocessing
{
public:
    AuditoryPreprocessing(Piano &piano);
    ~AuditoryPreprocessing(){};

    using SpectrumType = Key::SpectrumType;
    using Keys = Keyboard::Keys;
    const size_t NumberOfBins = Key::NumberOfBins;

public:

    // V2 (1.5): calcule la signature du cache spectrumcalculate.
    // Dépend de spectrum, recordedFrequency, measuredInharmonicity,
    // et de la version du prétraitement.
    std::string computeSpectrumCalculateSignature(int k) const;

    bool checkDataConsistency();        // check data consistency
    void normalizeSpectrum(Key &key);
    double getInharmonicPartial (double n,double f1,double B);
    double getInharmonicPartialIndex (double f, double f1, double B);
    void cleanSpectrum(Key &key);                // reduce noise in the spectra
    void cutLowFrequencies(Key &key);           // cut irrelevant low and high frequ

    void initializeSPLAFilter();    // define dBA filtering curve
    void convertToSPLA(SpectrumType &s);                 // compute A-weighted sound pressure
    void convertToLoudness (SpectrumType &s);

    void extrapolateInharmonicity();

    // V2 (cahier des charges 1.4): politique de renforcement des raies
    // hautes frequences.
    //   0 = legacy : ecrasement par le modele synthetique (comportement actuel,
    //       factor 0 -> 1 a partir de A4, donc A4 annule : bug C6)
    //   1 = off    : ne rien faire
    //   2 = complement : max(mesure, synthetique) uniquement au-dessus de
    //       5 kHz, sans ecraser ni annuler A4 (defaut V2)
    // Le mode est transmis par entropyminimizerv2.cpp depuis le XML.
    void improveHighFrequencyPeaks(const std::vector<bool>* keysToProcess = nullptr);

    // V2 (cahier des charges 1.5): scale = 1 reproduces the legacy width.
    // maxBassWidthCents caps the mollifier width in the bass (C7). 1e9 = no cap (legacy).
    // V2 (1.5): applyMollifier sur Key& (existant).
    void applyMollifier(Key& key, double scale = 1.0, double maxBassWidthCents = 50.0);

    // V2 (2.4): surcharge sur un SpectrumType&, pour la chaîne figée de
    // H_commun (EntropyEvaluator). Même calcul que la version Key&, mais
    // sans copie de Key.
    void applyMollifier(SpectrumType& spectrum, double scale, double maxBassWidthCents);

    // V2 (2.4): surcharge de normalizeSpectrum sur un SpectrumType&,
    // pour la chaîne figée de H_commun.
    void normalizeSpectrum(SpectrumType& spectrum);

    // V2 (cahier des charges 1.3): surcharge optionnelle de breakKeyIndex.
    // 0 (ou valeur <= 0) signifie "utiliser numberOfBassKeys du piano".
    void setBreakKeyIndex(int k) { mBreakKeyIndex = k; }

    // V2 (cahier des charges 1.3): politique de gestion de B.
    //   0 = ancien comportement (remplacement systématique par l'ajustement)
    //   1 = conserver B mesuré sauf si B <= 0 (défaut V2)
    void setInharmonicityPolicy(int p) { mInharmonicityPolicy = p; }
    int  getInharmonicityPolicy() const { return mInharmonicityPolicy; }

    // V2 (cahier des charges 1.4): politique de renforcement des raies HF.
//   0 = legacy (ecrasement, A4 annule), 1 = off, 2 = complement (defaut).
    void setHighFrequencyPeaksPolicy(int p) { mHighFrequencyPeaksPolicy = p; }
    int  getHighFrequencyPeaksPolicy() const { return mHighFrequencyPeaksPolicy; }

    // V2 (1.3): B LISSE, reserve a la courbe d'accord initiale (voir
    // extrapolateInharmonicity). Vide tant que extrapolateInharmonicity()
    // n'a pas ete appele.
    const std::vector<double>& getSmoothedInharmonicity() const { return mSmoothedInharmonicity; }


private:
    static double ftom (double f) { return Key::FrequencyToRealIndex(f); }
    static double mtof (int m)    { return Key::IndexToFrequency(m); }

    // V2 (2.4): convertToSPLA existe déjà sur SpectrumType&, donc pas besoin
    // de surcharge. On l'utilise directement.

    // V2 (1.5): version du prétraitement. À incrémenter à chaque modification
    // de la chaîne de prétraitement (normalize, clean, cut, SPLA, extrapolate,
    // improve), pour invalider les caches des anciennes versions.
    static const int PREPROCESSING_VERSION = 2;

    // V2 (1.3): détection de la rupture (plus grand saut de ln B).
    // Retourne l'index de la dernière touche du segment "cordes filées"
    // (la rupture est entre k et k+1). Repli sur numberOfBassKeys si
    // aucun saut valide n'est trouvé dans la fenêtre.
    int detectBreakKey(int numberOfBassKeys, int keyNumberOfA4);

    // V2 (1.3): ajustement Theil-Sen de y = a*x + b sur les points
    // (x_i, y_i) fournis. Retourne false si moins de 6 points.
    static bool theilSenFitSegment(const std::vector<double>& xs,
                                   const std::vector<double>& ys,
                                   double& a, double& b);

    // V2 (1.4): helper du mode "complement". Ne touche que les bins dont
    // la frequence est >= 5000 Hz, et applique spectrum[i] = max(spectrum[i],
    // synthetique) au lieu d'ecraser. Retourne le nombre de bins effectivement
    // renforces (touched) et le gain maximal (maxGain), pour la journalisation.
    void applyHighFrequencyComplement(Key& key, int& touched, double& maxGain);

    Piano &mPiano;
    Keyboard &mKeyboard;
    Keys &mKeys;
    int mNumberOfKeys;
    int mKeyNumberOfA4;
    std::vector<double> mdBA;           // vector holding dBA curve

    // V2 (1.3): 0 = auto (numberOfBassKeys du piano)
    int mBreakKeyIndex = 0;

    // V2 (1.3): politique de gestion de B (0 = ancien, 1 = conserver sauf B <= 0)
    int mInharmonicityPolicy = 1;

    // V2 (1.4): politique de renforcement des raies HF (0 = legacy, 1 = off,
// 2 = complement). Defaut = 2 (comportement V2.5).
    int mHighFrequencyPeaksPolicy = 2;

    // V2 (1.3): copie lissee de B pour ComputeInitialTuningCurve
    std::vector<double> mSmoothedInharmonicity;


};

}  // namespace entropyminimizerv2

#endif // AUDITORYPREPROCESSING_H
