#ifndef _KORALI_DISTRIBUTION_MULTINOMIAL_HPP_
#define _KORALI_DISTRIBUTION_MULTINOMIAL_HPP_

#include "modules/distributions/specific/base.hpp"

namespace Korali { namespace Distribution { namespace Specific {

class Multinomial : public Korali::Distribution::Specific::Base {

 public: 

 
 std::string getType() override;
 bool checkTermination() override;
 void getConfiguration(nlohmann::json& js) override;
 void setConfiguration(nlohmann::json& js) override;



 void getSelections(std::vector<double>& p, std::vector<unsigned int>& n, int N);
};

} } } // namespace Korali::Distribution::Specific

#endif // _KORALI_DISTRIBUTION_MULTINOMIAL_HPP_
