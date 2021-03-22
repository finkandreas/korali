#include "_environment/environment.hpp"
#include "korali.hpp"

int main(int argc, char *argv[])
{
  /////// Initializing environment

  _resultDir = "_result_vracer";
  initializeEnvironment("_config/dpd_2_d_eu_gaussian.json");

  auto e = korali::Experiment();

  ////// Checking if existing results are there and continuing them

  auto found = e.loadState(_resultDir + std::string("/latest"));
  if (found == true) printf("Continuing execution from previous run...\n");

  ////// Defining problem configuration

  e["Problem"]["Type"] = "Reinforcement Learning / Continuous";
  e["Problem"]["Environment Function"] = &runEnvironment;
  e["Problem"]["Training Reward Threshold"] = 1.6;
  e["Problem"]["Policy Testing Episodes"] = 20;

  //// Setting state variables

  e["Variables"][0]["Name"] = "Swimmer 1 - Pos X";
  e["Variables"][1]["Name"] = "Swimmer 1 - Pos Y";
  e["Variables"][2]["Name"] = "Swimmer 1 - Pos Z";
  e["Variables"][3]["Name"] = "Swimmer 1 - Quaternion X";
  e["Variables"][4]["Name"] = "Swimmer 1 - Quaternion Y";
  e["Variables"][5]["Name"] = "Swimmer 1 - Quaternion Z";
  e["Variables"][6]["Name"] = "Swimmer 1 - Quaternion W";
  e["Variables"][7]["Name"] = "Swimmer 2 - Pos X";
  e["Variables"][8]["Name"] = "Swimmer 2 - Pos Y";
  e["Variables"][9]["Name"] = "Swimmer 2 - Pos Z";
  e["Variables"][10]["Name"] = "Swimmer 2 - Quaternion X";
  e["Variables"][11]["Name"] = "Swimmer 2 - Quaternion Y";
  e["Variables"][12]["Name"] = "Swimmer 2 - Quaternion Z";
  e["Variables"][13]["Name"] = "Swimmer 2 - Quaternion W";

  //// Setting action variables

  e["Variables"][14]["Name"] = "Frequency (w)";
  e["Variables"][14]["Type"] = "Action";
  e["Variables"][14]["Lower Bound"] = 0.0f;
  e["Variables"][14]["Upper Bound"] = 2.0f;
  e["Variables"][14]["Initial Exploration Noise"] = 0.50f;

  e["Variables"][15]["Name"] = "Rotation X";
  e["Variables"][15]["Type"] = "Action";
  e["Variables"][15]["Lower Bound"] = -1.0f;
  e["Variables"][15]["Upper Bound"] = 1.0f;
  e["Variables"][15]["Initial Exploration Noise"] = 0.50f;

  e["Variables"][16]["Name"] = "Rotation Y";
  e["Variables"][16]["Type"] = "Action";
  e["Variables"][16]["Lower Bound"] = -1.0f;
  e["Variables"][16]["Upper Bound"] = 1.0f;
  e["Variables"][16]["Initial Exploration Noise"] = 0.50f;

  e["Variables"][17]["Name"] = "Rotation Z";
  e["Variables"][17]["Type"] = "Action";
  e["Variables"][17]["Lower Bound"] = -1.0f;
  e["Variables"][17]["Upper Bound"] = 1.0f;
  e["Variables"][17]["Initial Exploration Noise"] = 0.50f;

  /// Defining Agent Configuration

  e["Solver"]["Type"] = "Agent / Continuous / VRACER";
  e["Solver"]["Mode"] = "Training";
  e["Solver"]["Episodes Per Generation"] = 10;
  e["Solver"]["Updates Between Reward Rescaling"] = 20000;
  e["Solver"]["Experiences Between Policy Updates"] = 1;
  e["Solver"]["Episodes Between Policy Updates"] = 1;
  e["Solver"]["Learning Rate"] = 1e-4;
  e["Solver"]["Discount Factor"] = 0.99;
  e["Solver"]["L2 Regularization"]["Enabled"] = true;
  e["Solver"]["L2 Regularization"]["Importance"] = 1e-3;

  /// Defining the configuration of replay memory

  e["Solver"]["Experience Replay"]["Start Size"] = 131072;
  e["Solver"]["Experience Replay"]["Maximum Size"] = 262144;

  /// Configuring Mini Batch

  e["Solver"]["Mini Batch Size"] = 256;
  e["Solver"]["Mini Batch Strategy"] = "Uniform";

  /// Configuring the neural network and its hidden layers

  e["Solver"]["Neural Network"]["Engine"] = "OneDNN";

  e["Solver"]["Neural Network"]["Hidden Layers"][0]["Type"] = "Layer/Linear";
  e["Solver"]["Neural Network"]["Hidden Layers"][0]["Output Channels"] = 128;

  e["Solver"]["Neural Network"]["Hidden Layers"][1]["Type"] = "Layer/Activation";
  e["Solver"]["Neural Network"]["Hidden Layers"][1]["Function"] = "Elementwise/Tanh";

  e["Solver"]["Neural Network"]["Hidden Layers"][2]["Type"] = "Layer/Linear";
  e["Solver"]["Neural Network"]["Hidden Layers"][2]["Output Channels"] = 128;

  e["Solver"]["Neural Network"]["Hidden Layers"][3]["Type"] = "Layer/Activation";
  e["Solver"]["Neural Network"]["Hidden Layers"][3]["Function"] = "Elementwise/Tanh";

  ////// Defining Termination Criteria

  e["Solver"]["Termination Criteria"]["Testing"]["Target Average Reward"] = 1.6;

  ////// Setting file output configuration

  e["Console Output"]["Verbosity"] = "Detailed";
  e["File Output"]["Enabled"] = true;
  e["File Output"]["Frequency"] = 30;
  e["File Output"]["Path"] = _resultDir;

  auto k = korali::Engine();
  k.run(e);
}
