#include "engine.hpp"
#include "modules/solver/agent/discrete/dVRACER/dVRACER.hpp"
#include "omp.h"
#include "sample/sample.hpp"

namespace korali
{
namespace solver
{
namespace agent
{
namespace discrete
{
;

void dVRACER::initializeAgent()
{
  // Initializing common discrete agent configuration
  Discrete::initializeAgent();

  // Init statistics
  _statisticsAverageInverseTemperature = 0.;
  _statisticsAverageActionUnlikeability = 0.;

  /*********************************************************************
   * Initializing Critic/Policy Neural Network Optimization Experiment
   *********************************************************************/
  _criticPolicyLearner.resize(_problem->_policiesPerEnvironment);
  _criticPolicyExperiment.resize(_problem->_policiesPerEnvironment);
  _criticPolicyProblem.resize(_problem->_policiesPerEnvironment);

  for (size_t p = 0; p < _problem->_policiesPerEnvironment; p++)
  {
    _criticPolicyExperiment[p]["Problem"]["Type"] = "Supervised Learning";
    _criticPolicyExperiment[p]["Problem"]["Max Timesteps"] = _timeSequenceLength;
    if (_problem->_policiesPerEnvironment == 1)
      _criticPolicyExperiment[p]["Problem"]["Training Batch Size"] = _miniBatchSize * _problem->_agentsPerEnvironment;
    else
      _criticPolicyExperiment[p]["Problem"]["Training Batch Size"] = _miniBatchSize;
    _criticPolicyExperiment[p]["Problem"]["Inference Batch Size"] = 1;
    _criticPolicyExperiment[p]["Problem"]["Input"]["Size"] = _problem->_stateVectorSize;
    _criticPolicyExperiment[p]["Problem"]["Solution"]["Size"] = 1 + _policyParameterCount; // The value function, action q values, and inverse temperatur

    _criticPolicyExperiment[p]["Solver"]["Type"] = "Learner/DeepSupervisor";
    _criticPolicyExperiment[p]["Solver"]["L2 Regularization"]["Enabled"] = _l2RegularizationEnabled;
    _criticPolicyExperiment[p]["Solver"]["L2 Regularization"]["Importance"] = _l2RegularizationImportance;
    _criticPolicyExperiment[p]["Solver"]["Learning Rate"] = _currentLearningRate;
    _criticPolicyExperiment[p]["Solver"]["Loss Function"] = "Direct Gradient";
    _criticPolicyExperiment[p]["Solver"]["Steps Per Generation"] = 1;
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Optimizer"] = _neuralNetworkOptimizer;
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Engine"] = _neuralNetworkEngine;
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Hidden Layers"] = _neuralNetworkHiddenLayers;
    _criticPolicyExperiment[p]["Solver"]["Output Weights Scaling"] = 0.001;

    // No transformations for the state value output
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Transformation Mask"][0] = "Identity";
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Scale"][0] = 1.0f;
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Shift"][0] = 0.0f;

    // No transofrmation for the q values
    for (size_t i = 0; i < _problem->_actionCount; ++i)
    {
      _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Transformation Mask"][i + 1] = "Identity";
      _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Scale"][i + 1] = 1.0f;
      _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Shift"][i + 1] = 0.0f;
    }

    // Transofrmation for the inverse temperature
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Transformation Mask"][1 + _problem->_actionCount] = "Softplus"; // x = 0.5 * (x + std::sqrt(1. + x * x));
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Scale"][1 + _problem->_actionCount] = 1.0f;
    _criticPolicyExperiment[p]["Solver"]["Neural Network"]["Output Layer"]["Shift"][1 + _problem->_actionCount] = _initialInverseTemperature - 0.5;

    // Running initialization to verify that the configuration is correct
    _criticPolicyExperiment[p].initialize();
    _criticPolicyProblem[p] = dynamic_cast<problem::SupervisedLearning *>(_criticPolicyExperiment[p]._problem);
    _criticPolicyLearner[p] = dynamic_cast<solver::learner::DeepSupervisor *>(_criticPolicyExperiment[p]._solver);
  }
}

void dVRACER::trainPolicy()
{
  // Obtaining Minibatch experience ids
  const auto miniBatch = generateMiniBatch(_miniBatchSize);

  // Gathering state sequences for selected minibatch
  const auto stateSequence = getMiniBatchStateSequence(miniBatch);

  std::vector<policy_t> policyInfo = getPolicyInfo(miniBatch);

  // Running policy NN on the Minibatch experiences
  runPolicy(stateSequence, policyInfo);

  // Using policy information to update experience's metadata
  updateExperienceMetadata(miniBatch, policyInfo);

  // Now calculating policy gradients
  calculatePolicyGradients(miniBatch);

  for (size_t p = 0; p < _problem->_policiesPerEnvironment; p++)
  {
    // Updating learning rate for critic/policy learner guided by REFER
    _criticPolicyLearner[p]->_learningRate = _currentLearningRate;

    // Now applying gradients to update policy NN
    _criticPolicyLearner[p]->runGeneration();
  }
}

void dVRACER::calculatePolicyGradients(const std::vector<size_t> &miniBatch)
{
  const size_t miniBatchSize = miniBatch.size();

  // Init statistics
  _statisticsAverageInverseTemperature = 0.;
  _statisticsAverageActionUnlikeability = 0.;

#pragma omp parallel for reduction(+ \
                                   : _statisticsAverageInverseTemperature, _statisticsAverageActionUnlikeability)
  for (size_t b = 0; b < miniBatchSize; b++)
  {
    // Getting index of current experiment
    size_t expId = miniBatch[b];

    // Getting experience policy data
    const auto &expPolicy = _expPolicyVector[expId];
    // Getting current policy data
    const auto &curPolicy = _curPolicyVector[expId];

    // Getting value evaluation
    auto &V = _stateValueVector[expId];
    const std::vector<float> expVtbc = _retraceValueVector[expId];

    // If Cooporative setting Value is the sum of individual values
    if (_multiAgentRelationship == "Cooperation")
    {
      float avgV = 0.0f;
      for (size_t d = 0; d < _problem->_agentsPerEnvironment; d++)
        avgV += V[d];
      avgV /= _problem->_agentsPerEnvironment;
      V = std::vector<float>(_problem->_agentsPerEnvironment, avgV);
    }

    // If Multi Agent Correlation calculate product of importance weights
    float prodImportanceWeight = 1.0f;
    if (_multiAgentCorrelation)
    {
      for (size_t d = 0; d < _problem->_agentsPerEnvironment; d++)
        prodImportanceWeight *= _importanceWeightVector[expId][d];
    }

    for (size_t d = 0; d < _problem->_agentsPerEnvironment; d++)
    {
      // Storage for the update gradient
      std::vector<float> gradientLoss(1 + _policyParameterCount, 0.0f);

      // Gradient of Value Function V(s) (eq. (9); *-1 because the optimizer is maximizing)
      gradientLoss[0] = expVtbc[d] - V[d];

      //Gradient has to be divided by Number of Agents in Cooperation models
      if (_multiAgentRelationship == "Cooperation")
        gradientLoss[0] /= _problem->_agentsPerEnvironment;

      // Exploration annealing epsilon
      const float epsilon = getEpsilon();

      // Compute policy gradient only if inside trust region (or offPolicy disabled)
      if (_isOnPolicyVector[expId][d])
      {
        // Qret for terminal state is just reward
        float Qret = getScaledReward(_rewardVector[expId][d]);

        // If experience is non-terminal, add Vtbc
        if (_terminationVector[expId] == e_nonTerminal)
        {
          const float nextExpVtbc = _retraceValueVector[expId + 1][d];
          Qret += _discountFactor * nextExpVtbc;
        }

        // If experience is truncated, add truncated state value
        if (_terminationVector[expId] == e_truncated)
        {
          const float nextExpVtbc = _truncatedStateValueVector[expId][d];
          Qret += _discountFactor * nextExpVtbc;
        }

        // Compute Off-Policy Objective (eq. 5)
        const float lossOffPolicy = Qret - V[d];

        // Compute Policy Gradient wrt Params
        auto polGrad = calculateImportanceWeightGradient(curPolicy[d], expPolicy[d]);

        // If multi-agent correlation, multiply with additional factor
        if (_multiAgentCorrelation)
        {
          const float correlationFactor = prodImportanceWeight / _importanceWeightVector[expId][d];
          for (size_t i = 0; i < polGrad.size(); i++)
            polGrad[i] *= correlationFactor;
        }

        // Set Gradient of Loss wrt Params
        for (size_t i = 0; i < _policyParameterCount; i++)
        {
          // '-' because the optimizer is maximizing
          gradientLoss[1 + i] = _experienceReplayOffPolicyREFERCurrentBeta[d] * lossOffPolicy * polGrad[i] * epsilon;
        }
      }

      // Compute derivative of kullback-leibler divergence wrt current distribution params
      auto klGrad = calculateKLDivergenceGradient(expPolicy[d], curPolicy[d]);

      for (size_t i = 0; i < _policyParameterCount; i++)
      {
        // Step towards old policy (gradient pointing to larger difference between old and current policy)
        gradientLoss[1 + i] -= (1.0f - _experienceReplayOffPolicyREFERCurrentBeta[d]) * klGrad[i] * epsilon;

        if (std::isfinite(gradientLoss[i]) == false)
        {
          serializeExperienceReplay();
          _k->saveState();
          KORALI_LOG_ERROR("Gradient loss returned an invalid value: %f\n", gradientLoss[i]);
        }
      }
      // Set Gradient of Loss as Solution
      if (_problem->_policiesPerEnvironment == 1)
        _criticPolicyProblem[0]->_solutionData[b * _problem->_agentsPerEnvironment + d] = gradientLoss;
      else
        _criticPolicyProblem[d]->_solutionData[b] = gradientLoss;
    }

    // Update statistics
    for (size_t p = 0; p < _problem->_policiesPerEnvironment; ++p)
    {
      _statisticsAverageInverseTemperature += (curPolicy[p].distributionParameters[_problem->_actionCount] / (float)_problem->_policiesPerEnvironment);

      float unlikeability = 1.0;
      for (size_t i = 0; i < _problem->_actionCount; ++i)
        unlikeability -= curPolicy[p].actionProbabilities[i] * curPolicy[p].actionProbabilities[i];
      _statisticsAverageActionUnlikeability += (unlikeability / (float)_problem->_policiesPerEnvironment);
    }
  }

  // Compute statistics
  _statisticsAverageInverseTemperature /= (float)miniBatchSize;
  _statisticsAverageActionUnlikeability /= (float)miniBatchSize;
}

float dVRACER::calculateStateValue(const std::vector<std::vector<float>> &stateSequence, size_t policyIdx)
{
  // Forward the neural network for this state to get the state value
  const auto evaluation = _criticPolicyLearner[policyIdx]->getEvaluation({stateSequence});
  return evaluation[0][0];
}

void dVRACER::runPolicy(const std::vector<std::vector<std::vector<float>>> &stateSequenceBatch, std::vector<policy_t> &policyInfo, size_t policyIdx)
{
  // Getting batch size
  const size_t batchSize = stateSequenceBatch.size();

  // Preparing storage for results
  policyInfo.resize(batchSize);

  //inference operation (getAction)
  if (batchSize == 1)
  {
    const auto evaluation = _criticPolicyLearner[policyIdx]->getEvaluation(stateSequenceBatch);

    // Getting state value
    policyInfo[0].stateValue = evaluation[0][0];

    // Storage for action probabilities
    float maxq = -korali::Inf;
    std::vector<float> qValAndInvTemp(_policyParameterCount);
    std::vector<float> pActions(_problem->_actionCount);

    // Get the inverse of the temperature for the softmax distribution
    const float invTemperature = evaluation[0][_policyParameterCount];
    size_t maxIndex = 0;

    // Iterating all Q(s,a)
    for (size_t i = 0; i < _problem->_actionCount; i++)
    {
      // Computing Q(s,a_i)
      qValAndInvTemp[i] = evaluation[0][1 + i];

      // Extracting max Q(s,a_i)
      if (policyInfo[0].availableActions.size() > 0)
      {
        if (policyInfo[0].availableActions[i] == 1 && qValAndInvTemp[i] > maxq) 
        {
            maxq = qValAndInvTemp[i];
            maxIndex = i;
        }
      }
      else
      {
        if (qValAndInvTemp[i] > maxq) 
        {
            maxq = qValAndInvTemp[i];
            maxIndex = i;
        }
      }
    }

    // Storage for the cumulative e^Q(s,a_i)/maxq
    float sumExpQVal = 0.0;

    for (size_t i = 0; i < _problem->_actionCount; i++)
    {
      // Computing e^(beta(Q(s,a_i) - maxq))
      float expCurQVal = std::exp(invTemperature * (qValAndInvTemp[i] - maxq));
      if (policyInfo[0].availableActions.size() > 0)
        if (policyInfo[0].availableActions[i] == 0) expCurQVal = 0.;

      // Computing Sum_i(e^Q(s,a_i)/e^maxq)
      sumExpQVal += expCurQVal;

      // Storing partial value of the probability of the action
      pActions[i] = expCurQVal;
    }

    // Calculating inverse of Sum_i(e^Q(s,a_i))
    const float invSumExpQVal = 1.0f / sumExpQVal;

    // Exploration annealing epsilon
    const float epsilon = getEpsilon();

    // Normalizing action probabilities and annealing
    for (size_t i = 0; i < _problem->_actionCount; i++)
    {
      pActions[i] *= invSumExpQVal * epsilon;
      if (i == maxIndex)
        pActions[i] += 1.-epsilon;
    }

    // Set inverse temperature parameter
    qValAndInvTemp[_problem->_actionCount] = invTemperature;

    // Storing the action probabilities into the policy
    policyInfo[0].actionProbabilities = pActions;
    policyInfo[0].distributionParameters = qValAndInvTemp;
  }
  else //training operation
  {
    // Storage for each sample for all Policies
    std::vector<std::vector<std::vector<std::vector<float>>>> stateBatchDistributed(_problem->_policiesPerEnvironment);

    for (size_t b = 0; b < batchSize; b++)
    {
      stateBatchDistributed[b % _problem->_policiesPerEnvironment].push_back(stateSequenceBatch[b]);
    }

    for (size_t p = 0; p < _problem->_policiesPerEnvironment; p++)
    {
      const auto evaluation = _criticPolicyLearner[p]->getEvaluation(stateBatchDistributed[p]);
#pragma omp parallel for
      for (size_t b = 0; b < evaluation.size(); b++)
      {
        // Getting state value
        policyInfo[b * _problem->_policiesPerEnvironment + p].stateValue = evaluation[b][0];
        if (isfinite(policyInfo[b * _problem->_policiesPerEnvironment + p].stateValue) == false)
        {
          serializeExperienceReplay();
          _k->saveState();
          for (float e : evaluation[b]) printf("e: %f\n", e);
          KORALI_LOG_ERROR("State value not finite (%f) in policy evaluation during training step.", policyInfo[b * _problem->_policiesPerEnvironment + p].stateValue);
        }

        // Storage for action probabilities
        float maxq = -korali::Inf;
        std::vector<float> qValAndInvTemp(_policyParameterCount);
        std::vector<float> pActions(_problem->_actionCount);

        // Get the inverse of the temperature for the softmax distribution
        const float invTemperature = evaluation[b][_policyParameterCount];
        size_t maxIndex = 0;

        // Iterating all Q(s,a)
        for (size_t i = 0; i < _problem->_actionCount; i++)
        {
          // Copying Q(s,a_i)
          qValAndInvTemp[i] = evaluation[b][1 + i];

          // Extracting max Q(s,a_i)
          if (policyInfo[b].availableActions.size() > 0)
          {
            if (policyInfo[b].availableActions[i] == 1 && qValAndInvTemp[i] > maxq) 
            {
                maxq = qValAndInvTemp[i];
                maxIndex = i;
            }
          }
          else
          {
            if (qValAndInvTemp[i] > maxq) 
            {
                maxq = qValAndInvTemp[i];
                maxIndex = i;
            }
          }

          if (isfinite(qValAndInvTemp[i]) == false)
          {
            for (float e : evaluation[b]) printf("e: %f\n", e);
            for (size_t a : policyInfo[b].availableActions) printf("a: %zu\n", a);
            KORALI_LOG_ERROR("Q value not finite %f (%f) in policy evaluation during training step.", qValAndInvTemp[i], invTemperature);
          }
        }

        // Storage for the cumulative e^Q(s,a_i)/maxq
        float sumExpQVal = 0.0;

        for (size_t i = 0; i < _problem->_actionCount; i++)
        {
          // Computing e^(beta(Q(s,a_i) - maxq))
          float expCurQVal = std::exp(invTemperature * (qValAndInvTemp[i] - maxq));

          // Set probability zer if action not available
          if (policyInfo[b].availableActions.size() > 0)
            if (policyInfo[b].availableActions[i] == 0) expCurQVal = 0.;

          // Computing Sum_i(e^beta*Q(s,a_i)/e^maxq)
          sumExpQVal += expCurQVal;

          // Storing partial value of the probability of the action
          pActions[i] = expCurQVal;

          if (isfinite(pActions[i]) == false || isfinite(sumExpQVal) == false )
          {
            for (float e : evaluation[b]) printf("e: %f\n", e);
            for (size_t a : policyInfo[b].availableActions) printf("a: %zu\n", a);
            KORALI_LOG_ERROR("(1) pAction not finite %f (%f / %f / %f / %f) in policy evaluation during training step.", pActions[i], maxq, invTemperature, sumExpQVal, expCurQVal);
          }
        }

        // Calculating inverse of Sum_i(e^beta*Q(s,a_i))
        const float invSumExpQVal = 1.0f / sumExpQVal;
    
        // Exploration annealing epsilon
        const float epsilon = getEpsilon();

        // Normalizing action probabilities and annealing
        for (size_t i = 0; i < _problem->_actionCount; i++)
        {
          pActions[i] *= invSumExpQVal * epsilon;
          if (i == maxIndex) 
            pActions[i] += (1. - epsilon);

          if (isfinite(pActions[i]) == false)
          {
            for (float e : evaluation[b]) printf("e: %f\n", e);
            for (size_t a : policyInfo[b].availableActions) printf("a: %zu\n", a);
            KORALI_LOG_ERROR("(2) pAction not finite %f (%f / %f / %f / %f) in policy evaluation during training step.", pActions[i], maxq, invTemperature, invSumExpQVal, sumExpQVal);
          }
        }

        // Set inverse temperature parameter
        qValAndInvTemp[_problem->_actionCount] = invTemperature;

        // Storing the action probabilities into the policy
        policyInfo[b * _problem->_policiesPerEnvironment + p].actionProbabilities = pActions;
        policyInfo[b * _problem->_policiesPerEnvironment + p].distributionParameters = qValAndInvTemp;
      }
    }
  }
}

std::vector<policy_t> dVRACER::getPolicyInfo(const std::vector<size_t> &miniBatch) const
{
  // Getting mini batch size
  const size_t miniBatchSize = miniBatch.size();

  // Allocating policy sequence vector
  std::vector<policy_t> policyInfo(miniBatchSize * _problem->_agentsPerEnvironment);

#pragma omp parallel for
  for (size_t b = 0; b < miniBatchSize; b++)
  {
    // Getting current expId
    const size_t expId = miniBatch[b];

    // Filling policy information
    for (size_t d = 0; d < _problem->_agentsPerEnvironment; d++)
      policyInfo[b * _problem->_agentsPerEnvironment + d] = _expPolicyVector[expId][d];
  }

  return policyInfo;
}

knlohmann::json dVRACER::getAgentPolicy()
{
  knlohmann::json hyperparameters;
  for (size_t p = 0; p < _problem->_policiesPerEnvironment; p++)
    hyperparameters["Policy Hyperparameters"][p] = _criticPolicyLearner[p]->getHyperparameters();
  return hyperparameters;
}

void dVRACER::setAgentPolicy(const knlohmann::json &hyperparameters)
{
  for (size_t p = 0; p < _problem->_policiesPerEnvironment; p++)
    _criticPolicyLearner[p]->setHyperparameters(hyperparameters[p].get<std::vector<float>>());
}

void dVRACER::printAgentInformation()
{
  _k->_logger->logInfo("Normal", " + [dVRACER] Policy Learning Rate: %.3e\n", _currentLearningRate);
  _k->_logger->logInfo("Normal", " + [dVRACER] Average Inverse Temperature: %.3e\n", _statisticsAverageInverseTemperature);
  _k->_logger->logInfo("Normal", " + [dVRACER] Average Action Unlikeability: %.3e\n", _statisticsAverageActionUnlikeability);
}

void dVRACER::setConfiguration(knlohmann::json& js) 
{
 if (isDefined(js, "Results"))  eraseValue(js, "Results");

 if (isDefined(js, "Statistics", "Average Inverse Temperature"))
 {
 try { _statisticsAverageInverseTemperature = js["Statistics"]["Average Inverse Temperature"].get<float>();
} catch (const std::exception& e)
 { KORALI_LOG_ERROR(" + Object: [ dVRACER ] \n + Key:    ['Statistics']['Average Inverse Temperature']\n%s", e.what()); } 
   eraseValue(js, "Statistics", "Average Inverse Temperature");
 }

 if (isDefined(js, "Statistics", "Average Action Unlikeability"))
 {
 try { _statisticsAverageActionUnlikeability = js["Statistics"]["Average Action Unlikeability"].get<float>();
} catch (const std::exception& e)
 { KORALI_LOG_ERROR(" + Object: [ dVRACER ] \n + Key:    ['Statistics']['Average Action Unlikeability']\n%s", e.what()); } 
   eraseValue(js, "Statistics", "Average Action Unlikeability");
 }

 if (isDefined(js, "Initial Inverse Temperature"))
 {
 try { _initialInverseTemperature = js["Initial Inverse Temperature"].get<float>();
} catch (const std::exception& e)
 { KORALI_LOG_ERROR(" + Object: [ dVRACER ] \n + Key:    ['Initial Inverse Temperature']\n%s", e.what()); } 
   eraseValue(js, "Initial Inverse Temperature");
 }
  else   KORALI_LOG_ERROR(" + No value provided for mandatory setting: ['Initial Inverse Temperature'] required by dVRACER.\n"); 

 if (isDefined(js, "Annealing Time"))
 {
 try { _annealingTime = js["Annealing Time"].get<size_t>();
} catch (const std::exception& e)
 { KORALI_LOG_ERROR(" + Object: [ dVRACER ] \n + Key:    ['Annealing Time']\n%s", e.what()); } 
   eraseValue(js, "Annealing Time");
 }
  else   KORALI_LOG_ERROR(" + No value provided for mandatory setting: ['Annealing Time'] required by dVRACER.\n"); 

 Discrete::setConfiguration(js);
 _type = "agent/discrete/dVRACER";
 if(isDefined(js, "Type")) eraseValue(js, "Type");
 if(isEmpty(js) == false) KORALI_LOG_ERROR(" + Unrecognized settings for Korali module: dVRACER: \n%s\n", js.dump(2).c_str());
} 

void dVRACER::getConfiguration(knlohmann::json& js) 
{

 js["Type"] = _type;
   js["Initial Inverse Temperature"] = _initialInverseTemperature;
   js["Annealing Time"] = _annealingTime;
   js["Statistics"]["Average Inverse Temperature"] = _statisticsAverageInverseTemperature;
   js["Statistics"]["Average Action Unlikeability"] = _statisticsAverageActionUnlikeability;
 Discrete::getConfiguration(js);
} 

void dVRACER::applyModuleDefaults(knlohmann::json& js) 
{

 std::string defaultString = "{\"Initial Inverse Temperature\": 1.0, \"Annealing Time\": 10000000}";
 knlohmann::json defaultJs = knlohmann::json::parse(defaultString);
 mergeJson(js, defaultJs); 
 Discrete::applyModuleDefaults(js);
} 

void dVRACER::applyVariableDefaults() 
{

 std::string defaultString = "{}";
 knlohmann::json defaultJs = knlohmann::json::parse(defaultString);
 if (isDefined(_k->_js.getJson(), "Variables"))
  for (size_t i = 0; i < _k->_js["Variables"].size(); i++) 
   mergeJson(_k->_js["Variables"][i], defaultJs); 
 Discrete::applyVariableDefaults();
} 

bool dVRACER::checkTermination()
{
 bool hasFinished = false;

 hasFinished = hasFinished || Discrete::checkTermination();
 return hasFinished;
}

;

} //discrete
} //agent
} //solver
} //korali
;
