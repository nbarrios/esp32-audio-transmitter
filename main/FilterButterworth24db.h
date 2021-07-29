#ifndef __FILTERBUTTERWORTH24DB_H__
#define __FILTERBUTTERWORTH24DB_H__

class CFilterButterworth24db
{
public:
    CFilterButterworth24db(void);
    ~CFilterButterworth24db(void);
    void SetSampleRate(float fs);
    void Set(float cutoff, float q);
    float Run(float input);

private:
    float t0, t1, t2, t3;
    float coef0, coef1, coef2, coef3;
    float history1, history2, history3, history4;
    float gain;
    float min_cutoff, max_cutoff;
};

#endif // __FILTERBUTTERWORTH24DB_H__
