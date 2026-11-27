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
//                   Class describing a single piano key
//=============================================================================

#ifndef KEY_H
#define KEY_H

#include "prerequisites.h"

class Key;

template class EPT_EXTERN std::shared_ptr<Key>;

///////////////////////////////////////////////////////////////////////////////
/// \brief Class describing a single piano key
///
/// This class holds all information related to an individual piano key
/// of the keyboard. The most important element is the logarithmically binned
/// power spectrum of the key (in short: logbin spectrum). In contrast to the
/// ordinary FFT, which is linear in the frequency index, the logbin spectrum
/// is a vector whose index is logarithmic in the frequency. The resolution
/// is chosen in such a way that each vector component corresponds exactly
/// to one cent (1/100 half tone).
///
/// In addition, the Key class comprises various member variables such as the
/// mesured, the computed, and the tuned frequency.
///////////////////////////////////////////////////////////////////////////////

class EPT_EXTERN Key
{
public:
    // Charactereistics of the logarithmically binned spectrum

    static const int NumberOfBins;                  ///< Total number of slots: 9 octaves
    static const int BinsPerOctave;                 ///< Number of slots per ocatave (here 1 cent)
    static const double fmin;                       ///< Mimimal frequency of logbinned spectrum in Hz

    using SpectrumType = std::vector<double>;       ///< Type of a log-binned spectrum
    using PeakListType = std::map<double,double>;   ///< Type for a peak map

    // Conversion function in the context of the logarithmically binned spectrum

    static int FrequencyToIndex(double f);          ///< Convert frequency to logbin index
    static double FrequencyToRealIndex (double f);  ///< Convert frequency to real-valued logbin index
    static double IndexToFrequency (double m);      ///< Convert continuous slot index to frequency in Hz

public:

    Key()  { clear(); }                             ///< Constructor
    ~Key() {}                                       ///< Destructor
    void clear();                                   ///< Clear all data elements of the Key

    void setSpectrum (const SpectrumType &s);       ///< Copy spectrum to mSpectrum
    const SpectrumType &getSpectrum() const;        ///< Get a read-only reference to mSpectrum
    SpectrumType &getSpectrum();                    ///< Get a read-write reference of mSpectrum

    // V2 (cahier des charges 1.5/3.5): spectre avant mollification.
    // Ce champ est optionnel : s'il est vide, getSpectrumCalculate() retourne
    // un vecteur vide (PAS mSpectrum : voir remarque 1 du 27/11/2026).
    // Le cache est rempli par AuditoryPreprocessing après la chaîne de
    // prétraitement (normalize, clean, cut, SPLA, extrapolate, improve)
    // et AVANT applyMollifier.
    void setSpectrumCalculate(const SpectrumType& s);
    const SpectrumType& getSpectrumCalculate() const;
    SpectrumType& getSpectrumCalculate();
    bool hasSpectrumCalculate() const { return !mSpectrumCalculate.empty(); }

    // V2 (cahier des charges 1.5): signature du cache spectrumcalculate.
    // Elle dépend de spectrum, recordedFrequency, measuredInharmonicity
    // et de la version du prétraitement. Si elle ne correspond pas,
    // le cache est invalide et doit être recalculé.
    void setSpectrumCalculateSignature(const std::string& sig); // déclaration
    const std::string& getSpectrumCalculateSignature() const;   // déclaration
    bool hasSpectrumCalculateSignature() const;                 // déclaration

    void setPeaks (const PeakListType &s);          ///< Copy map of peaks
    const PeakListType &getPeaks() const;           ///< Get a read-only reference to mPeaks
    PeakListType &getPeaks();                       ///< Get a read-write reference of mPeaks

    void    setRecordedFrequency (const double f);  ///< Set recorded frequency
    double  getRecordedFrequency () const;          ///< Get recorded frequency
    double &getRecordedFrequency ();                ///< Get recorded frequency

    void    setMeasuredInharmonicity(double f);     ///< Set estimated inharmonicity
    double  getMeasuredInharmonicity () const;      ///< Get estimated inharmonicity
    double &getMeasuredInharmonicity ();            ///< Get estimated inharmonicity

    void    setRecognitionQuality(double f);        ///< Set quality of recognition
    double  getRecognitionQuality () const;         ///< Get quality of recognition
    double &getRecognitionQuality ();               ///< Get quality of recognition

    void    setComputedFrequency (double f);        ///< Set computed frequency
    double  getComputedFrequency () const;          ///< Get computed frequency
    double &getComputedFrequency ();                ///< Get computed frequency

    void    setTunedFrequency (double f);           ///< Set tuned frequency
    double  getTunedFrequency () const;             ///< Get tuned frequency
    double &getTunedFrequency ();                   ///< Get tuned frequency

    void    setOverpull (double cents);             ///< Set overpull in cents
    double  getOverpull () const;                   ///< Get overpull in cents
    double &getOverpull ();                         ///< Get overpull in cents

    void    setRecorded(bool r) {mRecorded = r;}    ///< Set recorded flag
    bool    isRecorded() const {return mRecorded;}  ///< Get recorded flag
    bool   &isRecorded() {return mRecorded;}        ///< Get recorded flag

private:
    SpectrumType mSpectrum;             ///< Logarithmically organized spectrum

    SpectrumType mSpectrumCalculate;            ///< Spectre avant mollification (V2 1.5)
    std::string mSpectrumCalculateSignature;    ///< Signature du cache (V2 1.5)

    PeakListType mPeaks;                ///< List of identified peaks
    double mRecordedFrequency;          ///< Recorded frequency of 1st partial in Hz
    double mMeasuredInharmonicity;      ///< Measured inharmonicity of recorded signal
    double mRecognitionQuality;         ///< Accuracy of higher partials (in cents)
    double mComputedFrequency;          ///< Computed frequency in Hz (tuning curve)
    double mTunedFrequency;             ///< Tuned frequency in Hz
    double mOverpull;                   ///< Overpull in cents
    bool   mRecorded;                   ///< Is the key already recorded?
};

#endif // KEY_H
