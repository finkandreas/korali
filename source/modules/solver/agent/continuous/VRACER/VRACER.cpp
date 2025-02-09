#include "engine.hpp"
#include "modules/solver/agent/continuous/VRACER/VRACER.hpp"
#include "omp.h"
#include "sample/sample.hpp"

#include <gsl/gsl_sf_psi.h>

namespace korali
{
namespace solver
{
namespace agent
{
namespace continuous
{
;

void VRACER::initializeAgent()
{
  // Initializing common discrete agent configuration
  Continuous::initializeAgent();

  // Init statistics
  _statisticsAverageActionSigmas.resize(_problem->_actionVectorSize);

  /*********************************************************************
   * Initializing Critic/Policy Neural Network Optimization Experiment
   *********************************************************************/

  _criticPolicyExperiment["Problem"]["Type"] = "Supervised Learning";
  _criticPolicyExperiment["Problem"]["Max Timesteps"] = _timeSequenceLength;
  _criticPolicyExperiment["Problem"]["Training Batch Size"] = _miniBatchSize;
  _criticPolicyExperiment["Problem"]["Inference Batch Size"] = 1;
  _criticPolicyExperiment["Problem"]["Input"]["Size"] = _problem->_stateVectorSize;
  _criticPolicyExperiment["Problem"]["Solution"]["Size"] = 1 + _policyParameterCount;

  _criticPolicyExperiment["Solver"]["Type"] = "Learner/DeepSupervisor";
  _criticPolicyExperiment["Solver"]["L2 Regularization"]["Enabled"] = _l2RegularizationEnabled;
  _criticPolicyExperiment["Solver"]["L2 Regularization"]["Importance"] = _l2RegularizationImportance;
  _criticPolicyExperiment["Solver"]["Learning Rate"] = _currentLearningRate;
  _criticPolicyExperiment["Solver"]["Loss Function"] = "Direct Gradient";
  _criticPolicyExperiment["Solver"]["Steps Per Generation"] = 1;
  _criticPolicyExperiment["Solver"]["Neural Network"]["Optimizer"] = _neuralNetworkOptimizer;
  _criticPolicyExperiment["Solver"]["Neural Network"]["Engine"] = _neuralNetworkEngine;
  _criticPolicyExperiment["Solver"]["Neural Network"]["Hidden Layers"] = _neuralNetworkHiddenLayers;
  _criticPolicyExperiment["Solver"]["Output Weights Scaling"] = 0.001;

  // No transformations for the state value output
  _criticPolicyExperiment["Solver"]["Neural Network"]["Output Layer"]["Scale"][0] = 1.0f;
  _criticPolicyExperiment["Solver"]["Neural Network"]["Output Layer"]["Shift"][0] = 0.0f;
  _criticPolicyExperiment["Solver"]["Neural Network"]["Output Layer"]["Transformation Mask"][0] = "Identity";

  // Setting transformations for the selected policy distribution output
  for (size_t i = 0; i < _policyParameterCount; i++)
  {
    _criticPolicyExperiment["Solver"]["Neural Network"]["Output Layer"]["Scale"][i + 1] = _policyParameterScaling[i];
    _criticPolicyExperiment["Solver"]["Neural Network"]["Output Layer"]["Shift"][i + 1] = _policyParameterShifting[i];
    _criticPolicyExperiment["Solver"]["Neural Network"]["Output Layer"]["Transformation Mask"][i + 1] = _policyParameterTransformationMasks[i];
  }

  // Running initialization to verify that the configuration is correct
  _criticPolicyExperiment.initialize();
  _criticPolicyProblem = dynamic_cast<problem::SupervisedLearning *>(_criticPolicyExperiment._problem);
  _criticPolicyLearner = dynamic_cast<solver::learner::DeepSupervisor *>(_criticPolicyExperiment._solver);

  _maxMiniBatchPolicyMean.resize(_problem->_actionVectorSize);
  _maxMiniBatchPolicyStdDev.resize(_problem->_actionVectorSize);

  _minMiniBatchPolicyMean.resize(_problem->_actionVectorSize);
  _minMiniBatchPolicyStdDev.resize(_problem->_actionVectorSize);
}

void VRACER::trainPolicy()
{
  // Obtaining Minibatch experience ids
  const auto miniBatch = generateMiniBatch(_miniBatchSize);

  // Gathering state sequences for selected minibatch
  const auto stateSequence = getMiniBatchStateSequence(miniBatch);

  // Running policy NN on the Minibatch experiences
  const auto policyInfo = runPolicy(stateSequence);

  // Using policy information to update experience's metadata
  updateExperienceMetadata(miniBatch, policyInfo);

  // Now calculating policy gradients
  calculatePolicyGradients(miniBatch);

  // Updating learning rate for critic/policy learner guided by REFER
  _criticPolicyLearner->_learningRate = _currentLearningRate;

  // Now applying gradients to update policy NN
  _criticPolicyLearner->runGeneration();
}

void VRACER::calculatePolicyGradients(const std::vector<size_t> &miniBatch)
{
  // Resetting statistics
  std::fill(_statisticsAverageActionSigmas.begin(), _statisticsAverageActionSigmas.end(), 0.0);

  const size_t miniBatchSize = miniBatch.size();

  for (size_t i = 0; i < _problem->_actionVectorSize; i++)
  {
    _maxMiniBatchPolicyMean[i] = -Inf;
    _maxMiniBatchPolicyStdDev[i] = -Inf;
    _minMiniBatchPolicyMean[i] = +Inf;
    _minMiniBatchPolicyStdDev[i] = +Inf;
  }

#pragma omp parallel for
  for (size_t b = 0; b < miniBatchSize; b++)
  {
    // Getting index of current experiment
    size_t expId = miniBatch[b];

    // Get state, action and policy for this experience
    const auto &expPolicy = _expPolicyVector[expId];
    const auto &expAction = _actionVector[expId];

    // Gathering metadata
    const float V = _stateValueVector[expId];
    const auto &curPolicy = _curPolicyVector[expId];
    const float expVtbc = _retraceValueVector[expId];

    // Storage for the update gradient
    std::vector<float> gradientLoss(1 + _policyParameterCount);

    // Gradient of Value Function V(s) (eq. (9); *-1 because the optimizer is maximizing)
    gradientLoss[0] = expVtbc - V;

    // Compute policy gradient only if inside trust region (or offPolicy disabled)
    if (_isOnPolicyVector[expId])
    {
      // Qret for terminal state is just reward
      float Qret = getScaledReward(_environmentIdVector[expId], _rewardVector[expId]);

      // If experience is non-terminal, add Vtbc
      if (_terminationVector[expId] == e_nonTerminal)
      {
        float nextExpVtbc = _retraceValueVector[expId + 1];
        Qret += _discountFactor * nextExpVtbc;
      }

      // If experience is truncated, add truncated state value
      if (_terminationVector[expId] == e_truncated)
      {
        float nextExpVtbc = _truncatedStateValueVector[expId];
        Qret += _discountFactor * nextExpVtbc;
      }

      // Compute Off-Policy Objective (eq. 5)
      float lossOffPolicy = Qret - V;

      // Compute IW Gradient wrt params
      auto polGrad = calculateImportanceWeightGradient(expAction, curPolicy, expPolicy);

      // Set Gradient of Loss wrt Params
      for (size_t i = 0; i < _policyParameterCount; i++)
        gradientLoss[1 + i] = _experienceReplayOffPolicyREFERBeta * lossOffPolicy * polGrad[i];
    }

    // Compute derivative of kullback-leibler divergence wrt current distribution params
    auto klGrad = calculateKLDivergenceGradient(expPolicy, curPolicy);

    // Step towards old policy (gradient pointing to larger difference between old and current policy)
    const float klGradMultiplier = -(1.0f - _experienceReplayOffPolicyREFERBeta);
    for (size_t i = 0; i < _policyParameterCount; i++)
      gradientLoss[1 + i] += klGradMultiplier * klGrad[i];

    for (size_t i = 0; i < _problem->_actionVectorSize; i++)
    {
      if (expPolicy.distributionParameters[i] > _maxMiniBatchPolicyMean[i]) _maxMiniBatchPolicyMean[i] = expPolicy.distributionParameters[i];
      if (expPolicy.distributionParameters[_problem->_actionVectorSize + i] > _maxMiniBatchPolicyStdDev[i]) _maxMiniBatchPolicyStdDev[i] = expPolicy.distributionParameters[_problem->_actionVectorSize + i];
      if (expPolicy.distributionParameters[i] < _minMiniBatchPolicyMean[i]) _minMiniBatchPolicyMean[i] = expPolicy.distributionParameters[i];
      if (expPolicy.distributionParameters[_problem->_actionVectorSize + i] < _minMiniBatchPolicyStdDev[i]) _minMiniBatchPolicyStdDev[i] = expPolicy.distributionParameters[_problem->_actionVectorSize + i];
    }

    // Set Gradient of Loss as Solution
    for (size_t i = 0; i < gradientLoss.size(); i++)
      if (std::isfinite(gradientLoss[i]) == false)
        KORALI_LOG_ERROR("Gradient loss returned an invalid value: %f\n", gradientLoss[i]);
    _criticPolicyProblem->_solutionData[b] = gradientLoss;
  }

  // Compute average action stadard deviation
  for (size_t j = 0; j < _problem->_actionVectorSize; j++) _statisticsAverageActionSigmas[j] /= (float)miniBatchSize;
}

std::vector<policy_t> VRACER::runPolicy(const std::vector<std::vector<std::vector<float>>> &stateBatch)
{
  // Getting batch size
  size_t batchSize = stateBatch.size();

  // Storage for policy
  std::vector<policy_t> policyVector(batchSize);

  // Forward the neural network for this state
  const auto evaluation = _criticPolicyLearner->getEvaluation(stateBatch);

#pragma omp parallel for
  for (size_t b = 0; b < batchSize; b++)
  {
    // Getting state value
    policyVector[b].stateValue = evaluation[b][0];

    // Getting distribution parameters
    policyVector[b].distributionParameters.assign(evaluation[b].begin() + 1, evaluation[b].end());
  }

  return policyVector;
}

knlohmann::json VRACER::getAgentPolicy()
{
  knlohmann::json hyperparameters;
  hyperparameters["Policy"] = _criticPolicyLearner->getHyperparameters();
  return hyperparameters;
}

void VRACER::setAgentPolicy(const knlohmann::json &hyperparameters)
{
  _criticPolicyLearner->setHyperparameters(hyperparameters["Policy"].get<std::vector<float>>());
}

void VRACER::printAgentInformation()
{
  _k->_logger->logInfo("Normal", " + [VRACER] Policy Learning Rate: %.3e\n", _currentLearningRate);
  _k->_logger->logInfo("Detailed", " + [VRACER] Max Policy Parameters (Mu & Sigma):\n");
  for (size_t i = 0; i < _problem->_actionVectorSize; i++)
    _k->_logger->logInfo("Detailed", " + [VRACER] Action %zu: (%.3e,%.3e)\n", i, _maxMiniBatchPolicyMean[i], _maxMiniBatchPolicyStdDev[i]);
  _k->_logger->logInfo("Detailed", " + [VRACER] Min Policy Parameters (Mu & Sigma):\n");
  for (size_t i = 0; i < _problem->_actionVectorSize; i++)
    _k->_logger->logInfo("Detailed", " + [VRACER] Action %zu: (%.3e,%.3e)\n", i, _minMiniBatchPolicyMean[i], _minMiniBatchPolicyStdDev[i]);
}

void VRACER::setConfiguration(knlohmann::json& js) 
{
 if (isDefined(js, "Results"))  eraseValue(js, "Results");

 if (isDefined(js, "Statistics", "Average Action Sigmas"))
 {
 try { _statisticsAverageActionSigmas = js["Statistics"]["Average Action Sigmas"].get<std::vector<float>>();
} catch (const std::exception& e)
 { KORALI_LOG_ERROR(" + Object: [ VRACER ] \n + Key:    ['Statistics']['Average Action Sigmas']\n%s", e.what()); } 
   eraseValue(js, "Statistics", "Average Action Sigmas");
 }

 if (isDefined(_k->_js.getJson(), "Variables"))
 for (size_t i = 0; i < _k->_js["Variables"].size(); i++) { 
 if (isDefined(_k->_js["Variables"][i], "Initial Exploration Noise"))
 {
 try { _k->_variables[i]->_initialExplorationNoise = _k->_js["Variables"][i]["Initial Exploration Noise"].get<float>();
} catch (const std::exception& e)
 { KORALI_LOG_ERROR(" + Object: [ VRACER ] \n + Key:    ['Initial Exploration Noise']\n%s", e.what()); } 
   eraseValue(_k->_js["Variables"][i], "Initial Exploration Noise");
 }
  else   KORALI_LOG_ERROR(" + No value provided for mandatory setting: ['Initial Exploration Noise'] required by VRACER.\n"); 

 } 
 Continuous::setConfiguration(js);
 _type = "agent/continuous/VRACER";
 if(isDefined(js, "Type")) eraseValue(js, "Type");
 if(isEmpty(js) == false) KORALI_LOG_ERROR(" + Unrecognized settings for Korali module: VRACER: \n%s\n", js.dump(2).c_str());
} 

void VRACER::getConfiguration(knlohmann::json& js) 
{

 js["Type"] = _type;
   js["Statistics"]["Average Action Sigmas"] = _statisticsAverageActionSigmas;
 for (size_t i = 0; i <  _k->_variables.size(); i++) { 
   _k->_js["Variables"][i]["Initial Exploration Noise"] = _k->_variables[i]->_initialExplorationNoise;
 } 
 Continuous::getConfiguration(js);
} 

void VRACER::applyModuleDefaults(knlohmann::json& js) 
{

 std::string defaultString = "{}";
 knlohmann::json defaultJs = knlohmann::json::parse(defaultString);
 mergeJson(js, defaultJs); 
 Continuous::applyModuleDefaults(js);
} 

void VRACER::applyVariableDefaults() 
{

 std::string defaultString = "{\"Initial Exploration Noise\": -1.0}";
 knlohmann::json defaultJs = knlohmann::json::parse(defaultString);
 if (isDefined(_k->_js.getJson(), "Variables"))
  for (size_t i = 0; i < _k->_js["Variables"].size(); i++) 
   mergeJson(_k->_js["Variables"][i], defaultJs); 
 Continuous::applyVariableDefaults();
} 

bool VRACER::checkTermination()
{
 bool hasFinished = false;

 hasFinished = hasFinished || Continuous::checkTermination();
 return hasFinished;
}

;

} //continuous
} //agent
} //solver
} //korali
;
