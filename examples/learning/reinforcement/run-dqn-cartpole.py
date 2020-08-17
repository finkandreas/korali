#!/usr/bin/env python3
import os
import sys
import math
import gym

######## Defining Environment Storage

cart = gym.make('CartPole-v1').unwrapped
maxSteps = 1000

####### Defining Problem's environment

def env(s):

 # Initializing environment
 seed = s["Sample Id"]
 cart.seed(seed)
 s["State"] = cart.reset().tolist()
 step = 0
 done = False

 while not done and step < maxSteps:

  # Getting new action
  s.update()
  
  # Reading action
  action = s["Action"][0] 
    
  # Performing the action
  state, reward, done, info = cart.step(action)

  # Storing Reward
  s["Reward"] = reward
   
  # Storing New State
  s["State"] = state.tolist()
  
  # Advancing step counter
  step = step + 1
  
import korali
k = korali.Engine()
e = korali.Experiment()

### Defining the Cartpole problem's configuration

e["Problem"]["Type"] = "Reinforcement Learning"
e["Problem"]["Environment Function"] = env

e["Variables"][0]["Name"] = "Cart Position"
e["Variables"][0]["Type"] = "State"

e["Variables"][1]["Name"] = "Cart Velocity"
e["Variables"][1]["Type"] = "State"

e["Variables"][2]["Name"] = "Pole Angle"
e["Variables"][2]["Type"] = "State"

e["Variables"][3]["Name"] = "Pole Angular Velocity"
e["Variables"][3]["Type"] = "State"

e["Variables"][4]["Name"] = "Push Direction"
e["Variables"][4]["Type"] = "Action"
e["Variables"][4]["Values"] = [ 0.0, 1.0 ]

### Configuring DQN hyperparameters

e["Solver"]["Type"] = "Agent/DQN"

### Defining Replay Memory configuration

e["Solver"]["Replay Memory"]["Start Size"] = 500
e["Solver"]["Replay Memory"]["Maximum Size"] = 150000

### Defining the configuration of the agent

e["Solver"]["Agent"]["Episodes Per Generation"] = 5 
e["Solver"]["Agent"]["Experience Limit"] = maxSteps

### Defining training policy configuration

e["Solver"]["Policy"]["Optimizer"]["Type"] = "Optimizer/Grid Search"
e["Solver"]["Policy"]["Epsilon"]["Initial Value"] = 1.0
e["Solver"]["Policy"]["Epsilon"]["Target Value"] = 0.05
e["Solver"]["Policy"]["Epsilon"]["Decrease Rate"] = 0.05

## Defining Q-Critic and Action-selection (policy) optimizers

e["Solver"]["Critic"]["Mini Batch Size"] = 32
e["Solver"]["Critic"]["Discount Factor"] = 0.99
e["Solver"]["Critic"]["Update Algorithm"] = "Retrace" 
e["Solver"]["Critic"]["Optimizer"]["Type"] = "Optimizer/Adam"
e["Solver"]["Critic"]["Optimizer"]["Eta"] = 0.1
e["Solver"]["Critic"]["Optimization Steps"] = 5

### Defining the shape of the neural network

e["Solver"]["Critic"]["Neural Network"]["Layers"][0]["Type"] = "Layer/Dense"
e["Solver"]["Critic"]["Neural Network"]["Layers"][0]["Node Count"] = 5
e["Solver"]["Critic"]["Neural Network"]["Layers"][0]["Activation Function"]["Type"] = "Linear"
e["Solver"]["Critic"]["Neural Network"]["Layers"][0]["Batch Normalization"]["Enabled"] = False

e["Solver"]["Critic"]["Neural Network"]["Layers"][1]["Type"] = "Layer/Dense"
e["Solver"]["Critic"]["Neural Network"]["Layers"][1]["Node Count"] = 32
e["Solver"]["Critic"]["Neural Network"]["Layers"][1]["Activation Function"]["Type"] = "Tanh"
e["Solver"]["Critic"]["Neural Network"]["Layers"][1]["Batch Normalization"]["Enabled"] = True

e["Solver"]["Critic"]["Neural Network"]["Layers"][2]["Type"] = "Layer/Dense"
e["Solver"]["Critic"]["Neural Network"]["Layers"][2]["Node Count"] = 32
e["Solver"]["Critic"]["Neural Network"]["Layers"][2]["Activation Function"]["Type"] = "Tanh"
e["Solver"]["Critic"]["Neural Network"]["Layers"][2]["Batch Normalization"]["Enabled"] = True

e["Solver"]["Critic"]["Neural Network"]["Layers"][3]["Type"] = "Layer/Dense"
e["Solver"]["Critic"]["Neural Network"]["Layers"][3]["Node Count"] = 1
e["Solver"]["Critic"]["Neural Network"]["Layers"][3]["Activation Function"]["Type"] = "Linear"
e["Solver"]["Critic"]["Neural Network"]["Layers"][3]["Batch Normalization"]["Enabled"] = True 

e["Solver"]["Critic"]["Normalization Steps"] = 32

### Defining Termination Criteria

e["Solver"]["Termination Criteria"]["Target Average Reward"] = 0.9*maxSteps

### Setting file output configuration

e["File Output"]["Frequency"] = 100

### Running Experiment

#k["Conduit"]["Type"] = "Concurrent"
#k["Conduit"]["Concurrent Jobs"] = 10
#k["Conduit"]["Type"] = "Distributed"
k.run(e)

###### Now running the cartpole experiment with Korali's help

state = cart.reset().tolist()
step = 0
done = False

while not done and step < maxSteps:
 action = int(e.getAction(state)[0])
 print('Step ' + str(step) + ' - State: ' + str(state) + ' - Action: ' + str(action), end = '')
 state, reward, done, info = cart.step(action)
 print('- Reward: ' + str(reward))
 step = step + 1