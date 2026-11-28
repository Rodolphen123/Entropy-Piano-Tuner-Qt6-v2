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

// mode debug
//#define DEBUG_EXTRAPOLATE_INHARMONICITY

#include "auditorypreprocessing.h"

#include <iostream>
#include <fstream>
#include <cstdint>   // V2 (1.8): FNV-1a, pour std::uint64_t / std::uint8_t

#include "core/system/eptexception.h"
#include "core/messages/messagehandler.h"
#include "core/messages/messagecaluclationprogress.h"
#include "core/piano/keyboard.h"
#include "core/piano/key.h"
#include "core/math/mathtools.h"
#include "core/config.h"

namespace entropyminimizerv2
{

//-----------------------------------------------------------------------------
//                              Constructor
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Constructor
///
/// Initializes the member variables.
///////////////////////////////////////////////////////////////////////////////

AuditoryPreprocessing::AuditoryPreprocessing (Piano &piano) :
    mPiano(piano),
    mKeyboard(mPiano.getKeyboard()),
    mKeys(mKeyboard.getKeys()),
    mNumberOfKeys(mKeyboard.getNumberOfKeys()),
    mKeyNumberOfA4(mKeyboard.getKeyNumberOfA4()),
    mdBA()
{
}


//-----------------------------------------------------------------------------
//			              Check consistency of dataset
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Check consistency of the piano dataset
///
/// This function tests the consistency of the incoming Piano dataset.
/// \return true if consistent.
///////////////////////////////////////////////////////////////////////////////
///
bool AuditoryPreprocessing::checkDataConsistency()
{
    // First check whether the number of keys is consistent.
    EptAssert(mKeys.size() > 0, "Piano should have at least one key");
    EptAssert(mKeys.size() == static_cast<size_t>(mNumberOfKeys),
              "Key vector length mismatch");

    // Next let us see whether all keys have been recorded.
    bool allkeysrecorded = true;
    bool somekeysrecorded = false;
    for (Key key : mKeys)
    {
        if (key.isRecorded()) somekeysrecorded = true;
        else allkeysrecorded = false;
    }
    if (not somekeysrecorded)
    {
        MessageHandler::send<MessageCaluclationProgress>
                (MessageCaluclationProgress::CALCULATION_FAILED,
                 MessageCaluclationProgress::CALCULATION_ERROR_NO_DATA);
        LogW("Calculation started without data");
        return false;
    }
    if (not allkeysrecorded)
    {
        MessageHandler::send<MessageCaluclationProgress>
                (MessageCaluclationProgress::CALCULATION_FAILED,
                 MessageCaluclationProgress::CALCULATION_ERROR_NOT_ALL_KEYS_RECORDED);
        LogW("Not all keys have been recorded");
        return false;
    }

    // Thirdly, let us see whether the frequencies and inharmonicities
    // are within reasonable bounds
    bool consistent = true;
    for (int keynumber=0; keynumber<mNumberOfKeys; keynumber++)
    {
        Key &key = mKeys[keynumber];
        double f = key.getRecordedFrequency();
        if (f<20 or f>20000)
        {
            LogW("Key %d: Frequency f=%f out of range.",keynumber,f);
            consistent = false;
        }
        double B = key.getMeasuredInharmonicity();
        if (B<0 or B>1)
        {
            LogW("Key %d: Inharmonicity B=%f out of range.",keynumber,B);
            consistent=false;
        }
        Key::SpectrumType &spectrum = key.getSpectrum();
        if (spectrum.size() != static_cast<size_t>(Key::NumberOfBins))
        {
            LogW("Key %d: Logspec size is %d, expected %d.",
                    keynumber,
                    static_cast<int>(spectrum.size()),
                    static_cast<int>(Key::NumberOfBins));
            consistent=false;
        }
        else if (MathTools::computeNorm(spectrum)==0)
        {
            LogW("Key %d: Logspec norm = %f.",keynumber,
                    MathTools::computeNorm(spectrum));
            consistent=false;
        }
    }

    // If data is found to be inconsistent send a corresponding message.
    if (not consistent)
    {
        MessageHandler::send<MessageCaluclationProgress>
                (MessageCaluclationProgress::CALCULATION_FAILED,
                 MessageCaluclationProgress::CALCULATION_ERROR_KEY_DATA_INCONSISTENT);
    }
    return consistent;
}


//-----------------------------------------------------------------------------
//                          Normalize all spectra
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Normalize spectrum
///
/// This function normalizes the logarithmic power spectra in such a way that
/// the sum over all components is equal to 1.
///////////////////////////////////////////////////////////////////////////////

void AuditoryPreprocessing::normalizeSpectrum(Key &key)
{
    Key::SpectrumType &spectrum = key.getSpectrum();
    MathTools::normalize(spectrum);
}


//-----------------------------------------------------------------------------
//            Compute frequency of inharmonic partial
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Compute frequency of inharmonic partial
///
/// This is the well-known function \f$f_n=f_1 \sqrt{\frac{1+B n^2}{1+B} } \f$.
///
/// \param n : Number of the partial
/// \param f1 : Frequency of the first partial
/// \param B : Inharmonicity coefficient
/// \return Frequency fn
///////////////////////////////////////////////////////////////////////////////

double AuditoryPreprocessing::getInharmonicPartial (double n, double f1, double B)
{
    return f1 * n * sqrt((1+B*n*n)/(1+B));
}


//-----------------------------------------------------------------------------
//            Compute the inharmonic index for a given frequency
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// This function is the inverse of the function InharmonicPartialFrequency.
/// It computes the number of the actual partial (as a floating point)
/// for given f, f1, and B. This is an exact inverse function.
///
/// \param f : Frequency of the n+th partial
/// \param f1 : Frequency of the first partial
/// \param B : Inharmonicity coefficient
/// \return Number of the actual partial as a floating point
///////////////////////////////////////////////////////////////////////////////

double AuditoryPreprocessing::getInharmonicPartialIndex (double f, double f1, double B)
{
    double x=f/f1;
    if (B==0) return x;
    else return 0.7071067811865475*sqrt((-1 + sqrt(1 + 4*B*(1+B)*x*x))/B);
}


//-----------------------------------------------------------------------------
//                              Clean spectrum
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Clean logarithmically binned spectrum
///
/// The purpose of this function is to cut away all contributions in the
/// logarithmically binned spectrum which are not close to the location of
/// expected partials (for given ground frequency f1 and inharmonicity B).
/// To this end we define an envelope function in the range [0,1] which
/// exhibits sharp peaks around the low-lying partials and broader peaks
/// for the higher partials. This ensures that possible inaccuracies in the
/// previous determination of the inharmonicity do not load to an
/// errorneous cancellation of higher partials.
///
/// \param key : Reference to the key
///////////////////////////////////////////////////////////////////////////////

void AuditoryPreprocessing::cleanSpectrum (Key &key)
{
    SpectrumType &spectrum = key.getSpectrum();
    const int M = static_cast<int>(spectrum.size());
    const double f = key.getRecordedFrequency();
    const double B = key.getMeasuredInharmonicity();

    auto wave = [f,B,this] (int m)
        { return cos(MathTools::PI*getInharmonicPartialIndex(mtof(m),f,B)); };
    auto envelope = [wave,f,this] (int m)
        { return  pow(fabs(wave(m)),200.0/pow(mtof(m)/f,1.5)); };

    for (int m=0; m<M; m++) spectrum[m] *= envelope(m);
}



//-----------------------------------------------------------------------------
//                         Cut frequencies below f1
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Cut all frequencies below 5/6*f1 in order to reduce noise
/// \param key : Reference to the key
///////////////////////////////////////////////////////////////////////////////

void AuditoryPreprocessing::cutLowFrequencies(Key &key)
{
    SpectrumType &spectrum = key.getSpectrum();
    const double f = key.getRecordedFrequency();
    // V2 (cahier des charges 1.6): cut below 5/6 of the frequency f, as
    // announced in the comment. The original code took 5/6 of the logarithmic
    // index, which cuts at a ratio between 0.95 (A0) and 0.41 (C8) instead.
    if (not (f > 0)) return;    // no valid recorded frequency: nothing to cut
    const int lowcutindex = std::min<int>(static_cast<int>(ftom(5.0*f/6.0)),static_cast<int>(NumberOfBins));
    for (int m=0; m<lowcutindex; m++) spectrum[m]=0;
}



//-----------------------------------------------------------------------------
//			          Initialize the filter function
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Initialize the filter function in the vector mdBA
///
/// Here we use ordinary dBA filtering. The empirical function is
/// given by \f[ _A(f)= {12200^2\cdot f^4\over (f^2+20.6^2)
/// \quad\sqrt{(f^2+107.7^2)\,(f^2+737.9^2)} \quad (f^2+12200^2)}\f]
/// For the sake of efficiency the values according to the logarithmically binned
/// spectra are stored in a vector mdBA.
///////////////////////////////////////////////////////////////////////////////

void AuditoryPreprocessing::initializeSPLAFilter()
{
    mdBA.clear();
    mdBA.resize(NumberOfBins);
    for (uint m=0; m<NumberOfBins; ++m)
    {
        double f = mtof(m);
        double Ra = 12200.0*12200.0*f*f*f*f / (f*f+20.6*20.6) /
               sqrt((f*f+107.7*107.7)*(f*f+737.9*737.9)) / (f*f+12200.0*12200.0);
        mdBA[m] = 2.0+20*log10(Ra);
        //std::cout << f << "\t" << mdBA[m] << std::endl;
    }
}


//-----------------------------------------------------------------------------
//              Compute A-weighted sound pressure level SPLA
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Compute A-weighted sound pressure level SPLA
///
/// This function computes the dBA-weighted sound pressure level (SPLA).
/// The SPLA is basically the logarithm of the power of each spectral line
/// with respect to a suitable lower cutoff. For simplicity the SPLA is
/// stored in the same spectrum vector of the local copy, destroying the
/// existing data which is not used for tuning.
///
/// \param spectrum : Reference to the spectrum
///////////////////////////////////////////////////////////////////////////////

void AuditoryPreprocessing::convertToSPLA (SpectrumType &spectrum)
{
    if (mdBA.size()==0) initializeSPLAFilter();
    EptAssert(mdBA.size()==NumberOfBins,"mdBA should be initialized.");
    const double I0 = 1E-7;	// auditory threshold intensity
    for (uint m=0; m<NumberOfBins; ++m)
    {
        double SPLA = 10 * log10(spectrum[m]/I0)  + mdBA[m];
        if (SPLA<0) spectrum[m]=0;
        else spectrum[m] = I0 * pow(10.0, SPLA/10.0);
    }
}

//-----------------------------------------------------------------------------
//               V2 (1.3) : ajustement Theil-Sen de y = a*x + b
//-----------------------------------------------------------------------------

namespace {

    // Theil-Sen : médiane des pentes deux à deux, puis médiane des résidus
    // pour l'ordonnée à l'origine. Robuste aux outliers, O(n^2) en temps.
    // Avec n <= 85 (clavier standard) c'est négligeable.
    int theilSenFit(const std::vector<double>& x,
        const std::vector<double>& y,
        double& a, double& b)
    {
        const int n = static_cast<int>(x.size());
        if (n < 2) { a = 0.0; b = 0.0; return n; }

        std::vector<double> slopes;
        slopes.reserve(static_cast<size_t>(n) * (n - 1) / 2);
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
            {
                const double dx = x[j] - x[i];
                if (dx != 0.0) slopes.push_back((y[j] - y[i]) / dx);
            }

        if (slopes.empty()) { a = 0.0; b = 0.0; return n; }

        std::sort(slopes.begin(), slopes.end());
        const size_t m = slopes.size();
        a = (m % 2 == 1) ? slopes[m / 2] : 0.5 * (slopes[m / 2 - 1] + slopes[m / 2]);

        std::vector<double> intercepts(n);
        for (int i = 0; i < n; ++i) intercepts[i] = y[i] - a * x[i];
        std::sort(intercepts.begin(), intercepts.end());
        b = (n % 2 == 1) ? intercepts[n / 2] : 0.5 * (intercepts[n / 2 - 1] + intercepts[n / 2]);

        return n;
    }

} // namespace

//-----------------------------------------------------------------------------
//          V2 (1.3) : helpers membres (détection rupture + fit segment)
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Détection de la rupture (plus grand saut de ln B).
///
/// On cherche, dans la fenêtre [12 .. keyNumberOfA4 - 8], la paire de
/// touches consécutives (k, k+1) qui maximise |ln B[k+1] - ln B[k]|,
/// en ne considérant que les B > 0. Retourne k (la rupture est entre k
/// et k+1). Si aucun saut valide n'est trouvé (moins de deux B > 0 dans
/// la fenêtre), retourne numberOfBassKeys (repli).
///////////////////////////////////////////////////////////////////////////////
int AuditoryPreprocessing::detectBreakKey(int numberOfBassKeys, int keyNumberOfA4)
{
    int searchStart = 12;
    int searchEnd   = keyNumberOfA4 - 8;
    if (searchEnd <= searchStart) searchEnd = searchStart + 1;
    if (searchEnd > mNumberOfKeys - 1) searchEnd = mNumberOfKeys - 1;

    int    bestKey  = numberOfBassKeys; // repli par défaut
    double bestJump = -1.0;

    for (int k = searchStart; k < searchEnd && k + 1 < mNumberOfKeys; ++k)
    {
        const double b0 = mKeys[k].getMeasuredInharmonicity();
        const double b1 = mKeys[k + 1].getMeasuredInharmonicity();
        if (!(b0 > 0.0) or !(b1 > 0.0)) continue; // ignorer les B manquants

        const double jump = std::fabs(std::log(b1) - std::log(b0));
        if (jump > bestJump)
        {
            bestJump = jump;
            bestKey  = k + 1; // première touche du segment suivant
        }
    }

    // V2 (1.3): si le saut maximal est trop faible, la détection n'est pas
    // fiable (bruit de mesure). On revient au repli numberOfBassKeys.
    // Seuil 0.6 : calé sur les 4 pianos de référence (Pleyel 1.55,
    // Erard 129257 1.38, Gaveau 0.99, Erard cordes // 0.50).
    if (bestJump < 0.6 && bestJump >= 0.0)
    {
        LogW("detectBreakKey: bestJump=%.3f trop faible, repli sur numberOfBassKeys=%d",
            bestJump, numberOfBassKeys);
        bestKey = numberOfBassKeys;
    }

    LogI("detectBreakKey: bestJump=%f at k=%d (repli numberOfBassKeys=%d)",
         bestJump, bestKey, numberOfBassKeys);
    return bestKey;
}

///////////////////////////////////////////////////////////////////////////////
/// \brief Ajustement Theil-Sen de y = a*x + b sur un segment.
/// Retourne false si moins de 6 points valides.
///////////////////////////////////////////////////////////////////////////////
bool AuditoryPreprocessing::theilSenFitSegment(const std::vector<double>& xs,
                                               const std::vector<double>& ys,
                                               double& a, double& b)
{
    if (xs.size() < 6 || xs.size() != ys.size())
    {
        a = 0.0; b = 0.0;
        return false;
    }
    theilSenFit(xs, ys, a, b);
    return true;
}

//-----------------------------------------------------------------------------
//               extrapolate missing inharmonicity estimates
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Extrapolate inharmonicity and estimate missing values
///
/// V2 (cahier des charges 1.3) :
///   - Politique 1 (défaut) : CONSERVER B mesuré, sauf B <= 0.
///     Les B <= 0 (touches > 2250 Hz, où estimateInharmonicity retourne 0
///     volontairement) sont remplis par :
///       * Theil-Sen sur le segment "cordes nues" à partir de
///         (rupture + 8), si au moins 6 points valides ;
///       * sinon getExpectedInharmonicity(f1).
///   - Politique 0 (ancien comportement) : REMPLACER tous les B par
///     l'ajustement robuste Theil-Sen en deux segments
///     [0 .. breakKeyIndex-1] et [breakKeyIndex .. N-1].
///
/// La rupture est détectée dans les données (plus grand saut de ln B,
/// fenêtre 12 .. A4-8) ; repli sur numberOfBassKeys si non trouvée.
/// breakKeyIndex (XML) surcharge la détection : si > 0, on l'utilise
/// directement comme point de rupture.
///
/// Journalisation : les B quasi nuls (conservés) et les B au plafond
/// de la grille sont signalés dans le log.
///////////////////////////////////////////////////////////////////////////////
void AuditoryPreprocessing::extrapolateInharmonicity()
{
    // Ancien code (conservé pour référence) :
    // const int firstkey = mKeyNumberOfA4-8;
    // double K=0, Y=0, KK=0, KY=0, N=0, BE=0;
    // for (int k = firstkey; k < mNumberOfKeys; ++k)
    // {
    //     if (N>1)
    //     {
    //         double a = (N*KY-K*Y)/(N*KK-K*K);
    //         double b = (KK*Y-K*KY)/(N*KK-K*K);
    //         BE = exp(a*k+b);
    //     }
    //     double B = mKeys[k].getMeasuredInharmonicity();
    //     bool B_is_valid = (B>0);
    //     if (B>0 and BE>0 and N>5) if (fabs(log(B/BE))>0.2) B_is_valid=false;
    //     if (B_is_valid)
    //     {
    //         double y = log(B);
    //         K+=k; Y+=y; KK+=k*k; KY+=k*y; N++;
    //     }
    //     else
    //     {
    //         if (BE==0)
    //         {
    //             double f1 = mPiano.getEqualTempFrequency(k,0,440);
    //             BE = mPiano.getExpectedInharmonicity(f1);
    //         }
    //         mKeys[k].setMeasuredInharmonicity(BE);
    //     }
    // }

    // --------------------------------------------------------------------
    // Étape 0 : déterminer la rupture.
    //   - si mBreakKeyIndex > 0 : surcharge XML, on l'utilise tel quel ;
    //   - sinon : détection dans les données (plus grand saut de ln B),
    //             repli sur numberOfBassKeys.
    // --------------------------------------------------------------------
    
#ifdef DEBUG_EXTRAPOLATE_INHARMONICITY
    std::ofstream os("0-original-inharmonicity.dat");
    for (int k = 0; k < mNumberOfKeys; ++k)
        os << k << " " << mKeys[k].getMeasuredInharmonicity() << std::endl;
#endif

    
    const int numberOfBassKeys = mKeyboard.getNumberOfBassKeys();
    int breakKeyIndex;
    if (mBreakKeyIndex > 0)
    {
        breakKeyIndex = mBreakKeyIndex;
        LogI("extrapolateInharmonicity: breakKeyIndex=%d (surcharge XML)",
             breakKeyIndex);
    }
    else
    {
        breakKeyIndex = detectBreakKey(numberOfBassKeys, mKeyNumberOfA4);
        LogI("extrapolateInharmonicity: breakKeyIndex=%d (détecté, repli=%d)",
             breakKeyIndex, numberOfBassKeys);
    }
    if (breakKeyIndex < 0) breakKeyIndex = 0;
    if (breakKeyIndex > mNumberOfKeys) breakKeyIndex = mNumberOfKeys;

    // --------------------------------------------------------------------
    // Étape 1 : collecter les points valides (k, ln B) par segment.
    // Le segment 1 = cordes filées [0 .. breakKeyIndex-1].
    // Le segment 2 = cordes nues   [breakKeyIndex .. N-1].
    // --------------------------------------------------------------------
    auto collectSegment = [this](int kBegin, int kEnd,
        std::vector<double>& xs,
        std::vector<double>& ys)
        {
            for (int k = kBegin; k < kEnd; ++k)
            {
                const double B = mKeys[k].getMeasuredInharmonicity();
                if (B > 0.0)
                {
                    xs.push_back(static_cast<double>(k));
                    ys.push_back(std::log(B));
                }
            }
        };

    std::vector<double> x1, y1, x2, y2;
    collectSegment(0, breakKeyIndex, x1, y1);              // cordes filées
    collectSegment(breakKeyIndex, mNumberOfKeys, x2, y2);  // cordes nues

    LogI("extrapolateInharmonicity: breakKeyIndex=%d, "
         "segment1=%d pts, segment2=%d pts, policy=%d",
         breakKeyIndex, static_cast<int>(x1.size()),
         static_cast<int>(x2.size()), mInharmonicityPolicy);

    // --------------------------------------------------------------------
    // Étape 2 : ajuster chaque segment.
    // Pour la politique 1, seul le segment 2 sert (à partir de rupture+8).
    // Pour la politique 0, les deux segments servent (comportement V2.5).
    // --------------------------------------------------------------------
    double a1 = 0.0, b1 = 0.0, a2 = 0.0, b2 = 0.0;
    bool fit1 = false, fit2 = false;

    if (mInharmonicityPolicy == 0)
    {
        // Politique 0 : deux segments complets (ancien comportement V2.5).
        fit1 = theilSenFitSegment(x1, y1, a1, b1);
        fit2 = theilSenFitSegment(x2, y2, a2, b2);
    }
    else
    {
        // Politique 1 : Theil-Sen sur le segment "cordes nues" uniquement,
          // à partir de rupture + 8 (marge). On ne touche pas aux basses.
        const int startFit = std::min(breakKeyIndex + 8, mNumberOfKeys);
        std::vector<double> xs, ys;
        collectSegment(startFit, mNumberOfKeys, xs, ys);
        fit2 = theilSenFitSegment(xs, ys, a2, b2);

        // V2 (1.3) : fit1 aussi, pour la détection d'outliers sur les graves.
        // Non utilisé pour le remplissage des B <= 0 en politique 1, mais
        // nécessaire pour comparer les B mesurés des cordes filées à leur
        // propre tendance (sinon fit2, extrapolé depuis les nues, détecte
        // à tort toutes les touches filées comme outliers).
        fit1 = theilSenFitSegment(x1, y1, a1, b1);

        LogI("extrapolateInharmonicity: policy=1, startFit=%d, "
            "fitPoints=%d, fitValid=%d, fit1Valid=%d",
            startFit, static_cast<int>(xs.size()),
            static_cast<int>(fit2), static_cast<int>(fit1));
    }

    // --------------------------------------------------------------------
// Étape 3 : appliquer la politique.
// --------------------------------------------------------------------
// Constantes de journalisation.
    const double NEAR_ZERO_B = 1e-8;      // seuil "quasi nul"
    // Facteur de détection des outliers (décision 1.3) : un B mesuré
    // est considéré comme atypique s'il sort de [fit/seuil ; seuil*fit].
    // Le seuil est élargi autour de la rupture (bonus décroissant) pour
    // ne pas détecter à tort les touches de transition filées -> nues.
    const double OUTLIER_FACTOR_BASE = 2.0;   // seuil de base (loin de la rupture)
    const double OUTLIER_BREAK_BONUS = 1.5;   // bonus maximal à la rupture

    int nKept = 0, nReplaced = 0, nFallback = 0, nOutliers = 0;

    for (int k = 0; k < mNumberOfKeys; ++k)
    {
        const double B = mKeys[k].getMeasuredInharmonicity();

        // Journalisation (politique 1 uniquement, sinon bruit inutile).
        if (mInharmonicityPolicy == 1)
        {
            if (B > 0.0 && B < NEAR_ZERO_B)
                LogW("Key %d : B quasi nul (%.3e) conservé", k, B);
            const double f1_ = mPiano.getEqualTempFrequency(k, 0, 440);
            const double gridCeil = 5.0 * mPiano.getExpectedInharmonicity(f1_);
            if (B > 0.0 && B >= gridCeil * 0.999)
                LogW("Key %d : B au plafond de la grille (%.3e, plafond=%.3e) conservé", k, B, gridCeil);
        }

        // -----------------------------------------------------------------
        // Politique 1 (keep) : conserver B sauf outlier ou B <= 0.
        // Ordre B : on traite les B > 0 atypiques ET les B <= 0 de la même
        // façon (remplacement par le fit). Les B remplis par le fit ne
        // peuvent pas être détectés comme outliers (ils sont dans
        // [fit/2 ; 2*fit] par construction).
        // -----------------------------------------------------------------
        if (mInharmonicityPolicy == 1)
        {
            // Valeur de remplacement : fit sur cordes nues si valide,
            // sinon getExpectedInharmonicity.
            auto replacementValue = [&]() -> double
                {
                    const bool inSeg1 = (k < breakKeyIndex);
                    const bool haveFit = inSeg1 ? fit1 : fit2;

                    double BE = 0.0;

                    if (haveFit)
                    {
                        const double aFit = inSeg1 ? a1 : a2;
                        const double bFit = inSeg1 ? b1 : b2;
                        BE = std::exp(aFit * static_cast<double>(k) + bFit);
                    }

                    if (!(BE > 0.0) || BE > 0.5)
                    {
                        const double f1_ = mPiano.getEqualTempFrequency(k, 0, 440);
                        BE = mPiano.getExpectedInharmonicity(f1_);
                    }

                    return BE;
                };

            // Cas 1 : B <= 0 (manquant) -> remplacer.
            if (!(B > 0.0))
            {
                const double BE = replacementValue();
                mKeys[k].setMeasuredInharmonicity(BE);
                ++nReplaced;
                LogI("Key %d : B manquant (%.3e) remplacé par %.3e", k, B, BE);
                continue;
            }

            // Cas 2 : B > 0 -> vérifier s'il est atypique.
            // On utilise le fit du segment auquel appartient la touche :
            // fit1 pour les cordes filées (< breakKeyIndex),
            // fit2 pour les cordes nues   (>= breakKeyIndex).
            const bool inSeg1 = (k < breakKeyIndex);
            const bool haveFit = inSeg1 ? fit1 : fit2;
            const double aFit = inSeg1 ? a1 : a2;
            const double bFit = inSeg1 ? b1 : b2;

            // Au-dessus de 2250 Hz, les B mesurés ne sont pas fiables.
            // On les remplace systématiquement par l'extrapolation.
            const double f1_ = mPiano.getEqualTempFrequency(k, 0, 440);

            if (f1_ > 2250.0)
            {
                const double BE = replacementValue();
                mKeys[k].setMeasuredInharmonicity(BE);
                ++nOutliers;

                LogI("Key %d : B mesuré non fiable (f=%.1f Hz, B=%.3e) remplacé par %.3e",
                    k, f1_, B, BE);
                continue;
            }

            if (haveFit)
            {
                const double fitted = std::exp(aFit * static_cast<double>(k) + bFit);
                const double dynamicThreshold = (k >= breakKeyIndex)
                    ? OUTLIER_FACTOR_BASE + OUTLIER_BREAK_BONUS / static_cast<double>(k - breakKeyIndex + 1)
                    : OUTLIER_FACTOR_BASE;

                if (fitted > 0.0 && (B < fitted / dynamicThreshold || B > fitted * dynamicThreshold))
                {
                    mKeys[k].setMeasuredInharmonicity(fitted);
                    ++nOutliers;
                    LogI("Key %d : B outlier (B=%.3e, fitted=%.3e) remplacé par %.3e",
                        k, B, fitted, fitted);
                    continue;
                }
            }

            // B > 0 et dans l'intervalle [fit/2 ; 2*fit] -> conserver.
            ++nKept;
            continue;
        }

        // -----------------------------------------------------------------
        // Politique 0 (replace) : ancien comportement, remplacement
        // systématique par le fit (deux segments).
        // -----------------------------------------------------------------
        const bool inSegment1 = (k < breakKeyIndex);
        const bool haveFit = inSegment1 ? fit1 : fit2;

        double BE = 0.0;
        if (haveFit)
        {
            const double a = inSegment1 ? a1 : a2;
            const double b = inSegment1 ? b1 : b2;
            BE = std::exp(a * static_cast<double>(k) + b);
        }
        else
        {
            const double f1_ = mPiano.getEqualTempFrequency(k, 0, 440);
            BE = mPiano.getExpectedInharmonicity(f1_);
        }

        if (!(BE > 0.0) or BE > 0.5)
        {
            const double f1_ = mPiano.getEqualTempFrequency(k, 0, 440);
            BE = mPiano.getExpectedInharmonicity(f1_);
        }
        mKeys[k].setMeasuredInharmonicity(BE);
    }

    LogI("extrapolateInharmonicity: policy=%d, kept=%d, replaced=%d, outliers=%d, fallback=%d",
        mInharmonicityPolicy, nKept, nReplaced, nOutliers, nFallback);

    LogI("extrapolateInharmonicity: fit1=%d (a1=%f b1=%f), fit2=%d (a2=%f b2=%f)",
         static_cast<int>(fit1), a1, b1, static_cast<int>(fit2), a2, b2);

    // ------------------------------------------------------------------------
    // V2 (1.3) : B LISSE pour la courbe d'accord initiale.
    // ComputeInitialTuningCurve() propage B d'octave en octave
    // (touche k-12 -> touche k) : une irregularite de B y est repetee et
    // amplifiee vers l'aigu. Conserver les mesures (politique 1) rend donc la
    // courbe initiale irreguliere, alors qu'elle sert de reference et d'ancrage
    // a la tolerance. Pour la courbe SEULEMENT, les B du segment ajuste qui
    // s'ecartent de plus de 0.2 (en ln, soit environ 22 %) de la droite sont
    // ramenes sur la droite, comme le faisait l'ancien moteur avec son rejet a
    // 20 %. Les B des touches (nettoyage, pics synthetiques) ne sont pas modifies.
    // ------------------------------------------------------------------------
    mSmoothedInharmonicity.assign(mNumberOfKeys, 0.0);
    for (int k = 0; k < mNumberOfKeys; ++k)
        mSmoothedInharmonicity[k] = mKeys[k].getMeasuredInharmonicity();

    if (mInharmonicityPolicy == 1 and fit2)
    {
        const int smoothStart = std::min(breakKeyIndex + 8, mNumberOfKeys);
        int nSmoothed = 0;
        for (int k = smoothStart; k < mNumberOfKeys; ++k)
        {
            const double line = std::exp(a2 * static_cast<double>(k) + b2);
            const double Bk = mSmoothedInharmonicity[k];
            if (line > 0.0 and line <= 0.5 and Bk > 0.0
                and std::fabs(std::log(Bk / line)) > 0.2)
            {
                mSmoothedInharmonicity[k] = line;
                ++nSmoothed;
            }
        }
        LogI("extrapolateInharmonicity: B lisse pour la courbe initiale : %d touche(s) ramenee(s) sur la droite",
             nSmoothed);
    }

#ifdef DEBUG_EXTRAPOLATE_INHARMONICITY
    {
        std::ofstream os("0-extrapolated-inharmonicity.dat");
        for (int k = 0; k < mNumberOfKeys; ++k)
            os << k << " " << mKeys[k].getMeasuredInharmonicity() << std::endl;
    }
#endif // DEBUG_EXTRAPOLATE_INHARMONICITY

}

//-----------------------------------------------------------------------------
//                  Improve high-frequency spectral lines
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Improve high-frequency spectral lines
///
/// Depending on the microphone the high-frequency spectral lines above 5kHz
/// are sometimes very weak.
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//          V2 (1.4) : helper du mode "complement"
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Renforce les raies hautes frequences sans ecraser le mesure.
///
/// Ne touche que les bins dont la frequence est >= mHighFrequencyCutoffHz
/// (5000 Hz par defaut, conformement au cahier des charges 1.4).
/// Pour chaque partiel n = 2..6, on calcule la position theorique mn et on
/// applique spectrum[i] = max(spectrum[i], synthetique) sur la fenetre
/// gaussienne [mn-10, mn+10]. Le fondamental n'est jamais annule (on ne
/// fait rien a i = m, et on ne met jamais 0). L'intensite de reference est
/// spectrum[m] (fondamental mesure), sans le facteur 0->1 du legacy.
///
/// \param key    : touche a traiter
/// \param touched: (sortie) nombre de bins effectivement renforces
/// \param maxGain: (sortie) gain maximal (synth - mesure) sur les bins touches
///////////////////////////////////////////////////////////////////////////////
void AuditoryPreprocessing::applyHighFrequencyComplement(Key& key,
    int& touched,
    double& maxGain)
{
    touched = 0;
    maxGain = 0.0;

    SpectrumType& spectrum = key.getSpectrum();
    const double f = key.getRecordedFrequency();
    const double B = key.getMeasuredInharmonicity();
    if (f <= 0 or B <= 0) return;

    const int m = MathTools::roundToInteger(ftom(f));
    if (m < 0 or m >= static_cast<int>(NumberOfBins)) return;

    const double intensity = spectrum[m];   // fondamental mesure, PAS de facteur 0->1
    if (!(intensity > 0.0)) return;

    // Frequence de coupure du CDC 1.4 (5 kHz). Constante locale : elle ne
    // depend pas du piano, elle est physiquement motivee (limite des micros).
    const double cutoffHz = 5000.0;

    for (int n = 2; n <= 6; ++n)
    {
        const double fn = getInharmonicPartial(n, f, B);
        if (fn < 20.0 or fn > 10000.0) continue;
        const int mn = MathTools::roundToInteger(ftom(fn));

        const double profile = intensity * pow(4.0, -n);   // amplitude au sommet
        for (int i = mn - 10; i <= mn + 10; ++i)
        {
            if (i < 0 or i >= static_cast<int>(NumberOfBins)) continue;
            if (mtof(i) < cutoffHz) continue;               // CDC : au-dessus de 5 kHz seulement

            const double synthetique = profile * exp(-0.1 * (i - mn) * (i - mn));
            if (synthetique > spectrum[i])
            {
                const double gain = synthetique - spectrum[i];
                if (gain > maxGain) maxGain = gain;
                spectrum[i] = synthetique;
                ++touched;
            }
        }
    }
}


//-----------------------------------------------------------------------------
//                  Improve high-frequency spectral lines
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Improve high-frequency spectral lines
///
/// V2 (cahier des charges 1.4) : trois modes, choisis par le XML
/// (highFrequencyPeaksPolicy) :
///   0 = legacy : ecrasement par le modele synthetique (comportement actuel).
///       ATTENTION : le facteur vaut 0 a A4, donc les partiels 2..6 de A4
///       sont mis a 0 (bug C6). A n'utiliser que pour comparer.
///   1 = off    : ne rien faire.
///   2 = complement : max(mesure, synthetique) uniquement au-dessus de
///       5 kHz, sans ecraser le mesure ni annuler A4. Comportement V2.
///
/// La journalisation indique, pour le mode complement, le nombre de bins
/// renforces et le gain max, par touche. Pour les autres modes, un simple
/// rappel de la politique suffit.
///////////////////////////////////////////////////////////////////////////////

void AuditoryPreprocessing::improveHighFrequencyPeaks(const std::vector<bool>* keysToProcess)
{
    LogI("improveHighFrequencyPeaks: policy=%d (0=legacy, 1=off, 2=complement)",
        mHighFrequencyPeaksPolicy);

    if (mHighFrequencyPeaksPolicy == 1) return;   // off

    const int start = mKeyNumberOfA4;

    if (mHighFrequencyPeaksPolicy == 2)
    {
        // Mode complement : max(mesure, synthetique) au-dessus de 5 kHz.
        int totalTouched = 0;
        int keyCount = 0;
        double maxGain = 0.0;
        for (int k = start; k < mNumberOfKeys; ++k)
        {
            if (keysToProcess && (k >= static_cast<int>(keysToProcess->size()) || !(*keysToProcess)[k]))
                continue;
            int touched = 0;
            double gain = 0.0;
            applyHighFrequencyComplement(mKeys[k], touched, gain);
            if (touched > 0) { ++keyCount; totalTouched += touched; }
            if (gain > maxGain) maxGain = gain;
        }
        LogI("improveHighFrequencyPeaks: mode=complement, keys_touched=%d, "
            "bins_touched=%d, maxGain=%.3e",
            keyCount, totalTouched, maxGain);
        return;
    }

    // Mode legacy : ecrasement (comportement actuel, C6 non corrige).
    for (int k = start; k < mNumberOfKeys; k++)
    {
        if (keysToProcess && (k >= static_cast<int>(keysToProcess->size()) || !(*keysToProcess)[k]))
            continue;
        Key& key = mKeys[k];
        SpectrumType& spectrum = key.getSpectrum();
        double f = key.getRecordedFrequency();
        double B = key.getMeasuredInharmonicity();
        if (f <= 0 or B <= 0) continue;
        int m = MathTools::roundToInteger(ftom(f));
        if (m<0 or m>static_cast<int>(NumberOfBins)) continue;
        double factor = (1.0 * (k - start)) / (mNumberOfKeys - start);
        double intensity = spectrum[m] * factor;
        for (int n = 2; n <= 6; ++n)
        {
            double fn = getInharmonicPartial(n, f, B);
            if (fn < 20 or fn>10000) continue;
            int mn = MathTools::roundToInteger(ftom(fn));
            for (int i = mn - 10; i <= mn + 10; ++i)
                if (i >= 0 and i < static_cast<int>(NumberOfBins))
                    spectrum[i] = intensity * pow(4, -n) * exp(-0.1 * (i - mn) * (i - mn));
        }
    }
    LogI("improveHighFrequencyPeaks: mode=legacy (ecrasement, C6)");
}



//-----------------------------------------------------------------------------
//                  Mollify
//-----------------------------------------------------------------------------

// V2 (2.4): implémentation commune, sur un SpectrumType&.
// La version Key& ci-dessous appelle celle-ci.
void AuditoryPreprocessing::applyMollifier(SpectrumType& spectrum,
    double scale,
    double maxBassWidthCents)
{
    LogD("applyMollifier: scale = %f, maxBassWidthCents = %f", scale, maxBassWidthCents);
    SpectrumType copy = spectrum;

    int M = static_cast<int>(NumberOfBins);
    for (int m = 0; m < M; ++m)
    {
        double f = mtof(m);
        double df = scale * (55.0 / f + f / 2000.0);
        int dm = MathTools::roundToInteger(ftom(f + df)) - m;
        if (dm < 1) dm = 1;
        if (dm > maxBassWidthCents)
            dm = static_cast<int>(maxBassWidthCents);
        double sum = 0, norm = 0;
        for (int ms = std::max(1, m - 3 * dm); ms <= std::min(m + 3 * dm, M - 1); ms++)
        {
            double weight = exp(-1.0 * (ms - m) * (ms - m) / dm / dm);
            norm += weight;
            sum += copy[ms] * weight;
        }
        if (norm > 0) spectrum[m] = sum / norm;
    }
}

// V2 (1.5): version Key&, qui appelle la surcharge SpectrumType&.
// Comportement strictement identique à avant.
void AuditoryPreprocessing::applyMollifier(Key& key,
    double scale,
    double maxBassWidthCents)
{
    applyMollifier(key.getSpectrum(), scale, maxBassWidthCents);
}

// V2 (2.4): surcharge de normalizeSpectrum sur un SpectrumType&.
void AuditoryPreprocessing::normalizeSpectrum(SpectrumType& spectrum)
{
    MathTools::normalize(spectrum);
}

//-----------------------------------------------------------------------------
//          V2 (1.8, point de suite 2): hash FNV-1a stable
//-----------------------------------------------------------------------------

// FNV-1a sur les octets bruts. Contrairement à std::hash<double>, le résultat
// est identique entre compilateurs et plateformes (mêmes bits -> même hash).
// Utilisé pour la signature du cache spectrumcalculate.
static std::uint64_t fnv1a_hash(const void* data, std::size_t len,
    std::uint64_t h = 1469598103934665603ULL)
{
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}


//-----------------------------------------------------------------------------
//          V2 (1.5): signature du cache spectrumcalculate
//-----------------------------------------------------------------------------

std::string AuditoryPreprocessing::computeSpectrumCalculateSignature(int k) const
{
    uint64_t h = 14695981039346656037ULL;
    auto mixBytes = [&h](const void* data, size_t len) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
        };
    auto mixDouble = [&mixBytes](double v) {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(double));
        mixBytes(&bits, sizeof(bits));
        };
    auto mixInt = [&mixBytes](int v) {
        mixBytes(&v, sizeof(v));
        };

    const Key& key = mKeys[k];
    for (double v : key.getSpectrum()) mixDouble(v);
    mixDouble(key.getRecordedFrequency());
    mixDouble(key.getMeasuredInharmonicity());
    mixInt(PREPROCESSING_VERSION);
    mixInt(mInharmonicityPolicy);
    mixInt(mHighFrequencyPeaksPolicy);
    mixInt(mBreakKeyIndex);
    mixInt(mKeyboard.getNumberOfBassKeys());
    mixInt(mNumberOfKeys);
    mixInt(mKeyNumberOfA4);

    std::ostringstream oss;
    oss << std::hex << h;
    return oss.str();
}




//-----------------------------------------------------------------------------
}  // namespace entropyminimizerv2
