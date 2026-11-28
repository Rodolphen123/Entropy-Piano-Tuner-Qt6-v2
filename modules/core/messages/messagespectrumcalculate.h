#ifndef MESSAGESPECTRUMCALCULATE_H
#define MESSAGESPECTRUMCALCULATE_H

#include <string>
#include "message.h"
#include "../piano/key.h"

class MessageSpectrumCalculate : public Message
{
public:
    MessageSpectrumCalculate(int keynumber,
        const Key::SpectrumType& spectrumCalculate,
        const std::string& signature) :
        Message(MSG_SPECTRUM_CALCULATE),
        mKeynumber(keynumber),
        mSpectrumCalculate(spectrumCalculate),
        mSignature(signature)
    {
    }

    ~MessageSpectrumCalculate() override = default;

    int getKeyNumber() const { return mKeynumber; }
    const Key::SpectrumType& getSpectrumCalculate() const { return mSpectrumCalculate; }
    const std::string& getSignature() const { return mSignature; }

private:
    const int mKeynumber;
    const Key::SpectrumType mSpectrumCalculate;
    const std::string mSignature;
};

#endif // MESSAGESPECTRUMCALCULATE_H

