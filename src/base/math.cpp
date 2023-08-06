#include "math.h"

static std::random_device RandomDevice;
static std::mt19937 RandomEngine(RandomDevice());
static std::uniform_real_distribution<float> DistributionFloat(0.0f, 1.0f);

float random_float()
{
	return DistributionFloat(RandomEngine);
}

bool random_prob(float f)
{
	return (random_float() < f);
}

int random_int(int Min, int Max)
{
	std::uniform_int_distribution<int> Distribution(Min, Max);
	return Distribution(RandomEngine);
}

int random_distribution(double* pProb, double* pProb2)
{
	std::discrete_distribution<int> Distribution(pProb, pProb2);
	return Distribution(RandomEngine);
}
