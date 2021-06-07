//  Korali environment for CubismUP-2D
//  Copyright (c) 2020 CSE-Lab, ETH Zurich, Switzerland.

#include "transportEnvironment.hpp"
#include "spline.h"
#include <chrono>
#include <filesystem>

int _argc;
char **_argv;

Simulation *_environment;
std::mt19937 _randomGenerator;

// Swimmer following an obstacle
void runEnvironment(korali::Sample &s)
{
  // Setting seed
  size_t sampleId = s["Sample Id"];
  _randomGenerator.seed(sampleId);

  // Creating results directory
  char resDir[64];
  sprintf(resDir, "%s/sample%08lu", s["Custom Settings"]["Dump Path"].get<std::string>().c_str(), sampleId);
  std::filesystem::create_directories(resDir);

  // Redirecting all output to the log file
  char logFilePath[128];
  sprintf(logFilePath, "%s/log.txt", resDir);
  auto logFile = freopen(logFilePath, "a", stdout);
  if (logFile == NULL)
  {
    printf("Error creating log file: %s.\n", logFilePath);
    exit(-1);
  }

  // Switching to results directory
  auto curPath = std::filesystem::current_path();
  std::filesystem::current_path(resDir);

  // Obtaining agent
  SmartCylinder* agent = dynamic_cast<SmartCylinder *>(_environment->getShapes()[0]);

  // Establishing environment's dump frequency
  _environment->sim.dumpTime = s["Custom Settings"]["Dump Frequency"].get<double>();

  // Reseting environment and setting initial conditions
  _environment->resetRL();
  std::vector<double> start{0.2,0.5};
  setInitialConditions(agent, start, s["Mode"] == "Training");

  // Set target 
  std::vector<double> target{0.8,0.5};

  // Setting initial state
  auto state = agent->state( target );
  s["State"] = state;

  // Setting initial time and step conditions
  double t = 0;        // Current time
  double tNextAct = 0; // Time until next action
  size_t curStep = 0;  // current Step

  // Setting maximum number of steps before truncation
  size_t maxSteps = 200;

  // Starting main environment loop
  bool done = false;
  while (done == false && curStep < maxSteps)
  {
    // Getting initial time
    auto beginTime = std::chrono::steady_clock::now(); // Profiling

    // Getting new action
    s.update();

    // Reading new action
    std::vector<double> action = s["Action"];

    // Setting action
    agent->act( action );

    // Run the simulation until next action is required
    tNextAct += 0.1;
    while ( t < tNextAct )
    {
      // Advance simulation
      const double dt = _environment->calcMaxTimestep();
      t += dt;

      // Advance simulation and check whether it is correct
      if (_environment->advance(dt))
      {
        fprintf(stderr, "Error during environment\n");
        exit(-1);
      }

      // Re-check if simulation is done.
      done = isTerminal( agent, target );
    }

    // Reward is +10 if state is terminal; otherwise obtain it from inverse distance to target
    double reward = done ? 100.0 : agent->reward( target );

    // Getting ending time
    auto endTime = std::chrono::steady_clock::now(); // Profiling
    double actionTime = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - beginTime).count() / 1.0e+9;

    // Printing Information:
    printf("[Korali] Sample %lu - Step: %lu/%lu\n", sampleId, curStep, maxSteps);
    printf("[Korali] State: [ %.3f", state[0]);
    for (size_t i = 1; i < state.size(); i++) printf(", %.3f", state[i]);
    printf("]\n");
    printf("[Korali] Force: [ %.3f, %.3f ]\n", action[0], action[1]);
    printf("[Korali] Reward: %.3f\n", reward);
    printf("[Korali] Terminal?: %d\n", done);
    printf("[Korali] Time: %.3fs\n", actionTime);
    printf("[Korali] -------------------------------------------------------\n");
    fflush(stdout);

    // Obtaining new agent state
    state = agent->state( target );

    // Storing reward
    s["Reward"] = reward;

    // Storing new state
    s["State"] = state;

    // Advancing to next step
    curStep++;
  }

  // Setting finalization status
  if (done == true)
    s["Termination"] = "Terminal";
  else
    s["Termination"] = "Truncated";

  // Switching back to experiment directory
  std::filesystem::current_path(curPath);

  // Closing log file
  fclose(logFile);
}

void setInitialConditions(SmartCylinder* agent, std::vector<double>& start, bool randomized)
{
  // Initial fixed conditions
  double locationX = start[0];
  double locationY = start[1];

  // or with noise
  if (randomized)
  {
    std::uniform_real_distribution<double> dis(-0.01, 0.01);

    double distX = dis(_randomGenerator);
    double distY = dis(_randomGenerator);

    locationX += distX;
    locationY += distY;
  }

  printf("[Korali] Initial Conditions:\n");
  printf("[Korali] locationX: %f\n", locationX);
  printf("[Korali] locationY: %f\n", locationY);

  // Setting initial position and orientation for the fish
  double C[2] = { locationX, locationY};
  agent->setCenterOfMass(C);

  // After moving the agent, the obstacles have to be restarted
  _environment->startObstacles();

  // Reset energy
  agent->energy = 0.;
}

bool isTerminal(SmartCylinder* agent, std::vector<double>& target )
{
  const double dX = (agent->center[0] - target[0]);
  const double dY = (agent->center[1] - target[1]);

  const double dTarget = std::sqrt(dX*dX+dY*dY);

  bool terminal = false;
  if ( dTarget < 1e-1 ) terminal = true;

  return terminal;
}

std::vector<double> logDivision(double start, double end, size_t nvertices)
{
    std::vector<double> vertices(nvertices, 0.0);
    for(size_t idx = 0; idx < nvertices; ++idx)
    {
        vertices[idx] = std::exp((double) idx / (double) (nvertices-1.0) * std::log(end-start+1.0)) - 1.0 + start;
	printf("v %zu %lf\n", idx, vertices[idx]);
    }
    return vertices;
}

void runEnvironmentMocmaes(korali::Sample &s)
{
  // Defining constants
  size_t sampleId = s["Sample Id"];
  double startX = 1.0;
  double endX = 3.0;
  double height = 2.0;

  // Constraints
  size_t maxSteps = 1e5;
  double maxEnergy = 1e-1;
 
  // Creating results directory
  std::string baseDir = "_log_transport_mocmaes/";
  char resDir[64];
  sprintf(resDir, "%s/sample%08lu", baseDir.c_str(), sampleId);
  std::filesystem::create_directories(resDir); 
  // Redirecting all output to the log file
  char logFilePath[128];
  sprintf(logFilePath, "%s/log.txt", resDir);
  auto logFile = freopen(logFilePath, "a", stdout);
  if (logFile == NULL)
  {
    printf("Error creating log file: %s.\n", logFilePath);
    exit(-1);
  }

  // Switching to results directory
  auto curPath = std::filesystem::current_path();
  std::filesystem::current_path(resDir);

  // Obtaining agent
  SmartCylinder* agent = dynamic_cast<SmartCylinder *>(_environment->getShapes()[0]);

  // Establishing environment's dump frequency
  _environment->sim.dumpTime = 0.0;

  // Reseting environment and setting initial conditions
  _environment->resetRL();
  std::vector<double> start{startX, height};
  setInitialConditions(agent, start, false);

  // Set target 
  std::vector<double> target{endX, height};

  // Parametrization of force
  std::vector<double> params = s["Parameters"];
  size_t numParams = params.size();

  double* centerArr = agent->center;
  std::vector<double> currentPos(centerArr, centerArr+2);
  
  //std::vector<double> vertices = logDivision(startX, endX, numParams+1);
  std::vector<double> edges(numParams, 0.0);
  for(size_t i = 0; i < numParams; ++i) edges[i] = startX + i*(endX-startX)/(float)(numParams-1.);

  // Natural cubic spline (C^2) with natural boundary conditions (f''=0)
  tk::spline forceSpline(edges,params);

  // Init counting variables
  double distToTarget = distance(currentPos, target);
  double energy = 0.0; // Total energy
  double t = 0.0;      // Current time
  size_t curStep = 0;  // Current Step

  // Starting main environment loop
  bool done = false;
  std::vector<double> action(2, 0.0);

  while (done == false)
  {
    centerArr = agent->center;
    currentPos[0] = centerArr[0];
    currentPos[1] = centerArr[1];

    double force = std::abs(forceSpline(currentPos[0])); // std::abs because spline evaluation may be negative

    if (distToTarget > 0.)
    {
        // Split force in x & y component
        action[0] = force*(target[0]-currentPos[0])/distToTarget;
        action[1] = force*(target[1]-currentPos[1])/distToTarget;
    }
    else
    {
        // Safe split in case of close distance
        action[0] = force*(target[0]-currentPos[0]);
        action[1] = force*(target[1]-currentPos[1]);
    }

    // Setting action
    agent->act( action );
    double dt = _environment->calcMaxTimestep();
    t += dt;
 
    // Update distance and check termination
    bool error = _environment->advance(dt);
    distToTarget = distance(currentPos, target);
    energy = agent->energy;

    // Checkting termination
    done = (currentPos[0] >= endX) || (curStep >= maxSteps) || (energy >= maxEnergy);
 
    curStep++;
    
    // Printing Information:
    printf("[Korali] Sample %lu, Step: %lu/%lu\n", sampleId, curStep, maxSteps);
    printf("[Korali] State: [ %.6f, %.6f ]\n", currentPos[0], currentPos[1]);
    printf("[Korali] Force: [ %.6f, %.6f ]\n", action[0], action[1]);
    printf("[Korali] Energy %f, Distance %f, Terminal?: %d\n", energy, distToTarget, done);
    printf("[Korali] Time: %.3fs\n", t);
    printf("[Korali] -------------------------------------------------------\n");
    fflush(stdout);
 
    if (error == true)
    {
      fprintf(stderr, "Error during environment\n");
      exit(-1);
    }

  }
  
  // Penalization for not reaching target
  if (currentPos[0] < endX)
  {
    printf("Target not reached, penalizing objectives..\n");
	t += (endX-currentPos[0])*1e9;
	energy += (endX-currentPos[0])*1e9;
  }
  if (energy > maxEnergy)
  {
    printf("Max energy violated (%f), penalizing objectives..\n", maxEnergy);
	t += (energy-maxEnergy)*1e9;
	energy += (energy-maxEnergy)*1e9;
  }
  
  // Setting Objectives
  std::vector<double> objectives = { -t, -energy };
  printf("Objectives: %f (time), %f (energy) (total steps %zu) \n", t, energy, curStep);
  s["F(x)"] = objectives;

  // Switching back to experiment directory
  std::filesystem::current_path(curPath);

  // Closing log file
  fclose(logFile);

}

void runEnvironmentCmaes(korali::Sample& s)
{
  // Defining constants
  size_t sampleId = s["Sample Id"];
  double startX = 1.0;
  double endX = 3.0;
  double height = 2.0;

  // Constraints
  size_t maxSteps = 1e5;
 
  // Creating results directory
  std::string baseDir = "_log_transport_cmaes/";
  char resDir[64];
  sprintf(resDir, "%s/sample%08lu", baseDir.c_str(), sampleId);
  std::filesystem::create_directories(resDir); 
  // Redirecting all output to the log file
  char logFilePath[128];
  sprintf(logFilePath, "%s/log.txt", resDir);
  auto logFile = freopen(logFilePath, "a", stdout);
  if (logFile == NULL)
  {
    printf("Error creating log file: %s.\n", logFilePath);
    exit(-1);
  }

  // Switching to results directory
  auto curPath = std::filesystem::current_path();
  std::filesystem::current_path(resDir);

  // Obtaining agent
  SmartCylinder* agent = dynamic_cast<SmartCylinder *>(_environment->getShapes()[0]);

  // Establishing environment's dump frequency
  _environment->sim.dumpTime = 0.0;

  // Reseting environment and setting initial conditions
  _environment->resetRL();
  std::vector<double> start{startX, height};
  setInitialConditions(agent, start, false);

  // Parametrization of force
  std::vector<double> params = s["Parameters"];
  const double a = params[0];
  const double b = params[1];
  const double c = params[2];
  const double d = params[3];
  const double e = params[4];

  double* centerArr = agent->center;
  std::vector<double> currentPos(centerArr, centerArr+2);
  
  // Force applied
  const double maxForce = 1e-2;
  
  // Safety intervall before boundary (eps + radius)
  const double deps = 3e-1;
  
  // Init counting variables
  double energy = 0.0; // Total energy
  double t = 0.0;      // Current time
  size_t curStep = 0;  // Current Step

  // Starting main environment loop
  bool done = false;
  std::vector<double> action(2, 0.0);

  while (done == false)
  {
    centerArr = agent->center;
    currentPos[0] = centerArr[0];
    currentPos[1] = centerArr[1];
 
    const double x = currentPos[0];
    double forcex = 1.;
    double forcey = (d*x+e)*(0.5*a/std::sqrt(x)+b+2.*c*x)*std::cos(a*std::sqrt(x)+x*b+c*x*x)+d*std::sin(a*std::sqrt(x)+x*b+c*x*x);

    // Force vector normalization
    double FvecLength = std::sqrt(forcey*forcey+forcex*forcex);
    forcey /= FvecLength;
    forcex /= FvecLength;

    // Split force in x & y component
    action[0] = forcex*maxForce;
    action[1] = forcey*maxForce;

    // Setting action
    agent->act( action );
    double dt = _environment->calcMaxTimestep();
    t += dt;
 
    // Update distance and check termination
    bool error = _environment->advance(dt);
    energy = agent->energy;

    // Checkting termination
    done = (currentPos[0] >= endX) || (curStep >= maxSteps);
 
    curStep++;
    
    // Printing Information:
    printf("[Korali] Sample %lu, Step: %lu/%lu\n", sampleId, curStep, maxSteps);
    printf("[Korali] State: [ %.6f, %.6f ]\n", currentPos[0], currentPos[1]);
    printf("[Korali] Force: [ %.6f, %.6f ]\n", action[0], action[1]);
    printf("[Korali] Energy %f, Terminal?: %d\n", energy, done);
    printf("[Korali] Time: %.3fs\n", t);
    printf("[Korali] -------------------------------------------------------\n");
    fflush(stdout);
 
    if (error == true)
    {
      fprintf(stderr, "Error during environment\n");
      exit(-1);
    }

    if (currentPos[0] < deps) 
    {
        done = true; // cylinder approaching left bound
        printf("[Korali] Terminating, Cylinder approaching left bound\n");
    }
    if (currentPos[1] > 4.0 - deps)
    {
        done = true; // cylinder approaching upper bound
        printf("[Korali] Terminating, Cylinder approaching upper bound\n");
    }
    else if (currentPos[1] < deps)
    {
        done = true; // cylinder approaching lower bound
        printf("[Korali] Terminating, Cylinder approaching lower bound\n");
    }

  }
  
  // Penalization for not reaching target
  if (currentPos[0] < endX)
  {
    printf("Target not reached, penalizing objectives..\n");
	t += (endX-currentPos[0])*1e9;
  }
  
  // Setting Objectives
  printf("Objectives: %f (time), %f (energy) (total steps %zu) \n", t, energy, curStep);
  s["F(x)"] = -t;

  // Switching back to experiment directory
  std::filesystem::current_path(curPath);

  // Closing log file
  fclose(logFile);

}
