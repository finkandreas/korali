#!/usr/bin/env python3

import math
import pdb
import numpy as np
import os
import sys
from PIL import Image
import matplotlib.pyplot as plt

def initEnvironment(e, envName, model = ''):

 # Creating environment 
 if (envName ==  'Waterworld') and model == '' :
    from pettingzoo.sisl import waterworld_v3
    
    env = waterworld_v3.env(local_ratio = 0.0)
    obs_upper = 2 * math.sqrt(2)
    obs_low = -1 * math.sqrt(2)
    ac_upper = 0.01 
    ac_low = -0.01 
    numIndividuals = 5
    stateVariableCount = 242 * numIndividuals
    actionVariableCount = 2 * numIndividuals
    numIndividuals = 1

 elif (envName ==  'Waterworld') and model == '1' :
    from pettingzoo.sisl import waterworld_v3
    
    env = waterworld_v3.env()
    obs_upper = 2 * math.sqrt(2)
    obs_low = -1 * math.sqrt(2)
    ac_upper = 0.01 
    ac_low = -0.01 
    numIndividuals = 5
    stateVariableCount = 242 * (numIndividuals + 1)
    actionVariableCount = 2 
 
 elif (envName ==  'Waterworld') and model == '2' :
    from pettingzoo.sisl import waterworld_v3
    
    env = waterworld_v3.env()
    obs_upper = 2 * math.sqrt(2)
    obs_low = -1 * math.sqrt(2)
    ac_upper = 0.01 
    ac_low = -0.01 
    numIndividuals = 5
    stateVariableCount = 242 * (numIndividuals)
    actionVariableCount = 2 

 elif (envName == 'Multiwalker'):
    from pettingzoo.sisl import multiwalker_v7
    env = multiwalker_v7.env()
    stateVariableCount = 31
    actionVariableCount = 4
    obs_upper =  math.inf
    obs_low = -1 * math.inf
    ac_upper = 1 
    ac_low = -1 
    numIndividuals = 3

 else:
   print("Environment '{}' not recognized! Exit..".format(envName))
   sys.exit()
 
 
 
 ### Defining problem configuration for openAI Gym environments
 e["Problem"]["Type"] = "Reinforcement Learning / Continuous"
 e["Problem"]["Environment Function"] = lambda x : agent(x, env, model)
 e["Problem"]["Custom Settings"]["Print Step Information"] = "Disabled"
 e["Problem"]["Training Reward Threshold"] = math.inf
 #e["Problem"]["Testing Frequency"] = 2
 e["Problem"]["Policy Testing Episodes"] = 20
 e["Problem"]["Agents Per Environment"] = numIndividuals
 
 # Generating state variable index list
 stateVariablesIndexes = range(stateVariableCount)
 
 # Defining State Variables
 
 for i in stateVariablesIndexes:
  e["Variables"][i]["Name"] = "State Variable " + str(i)
  e["Variables"][i]["Type"] = "State"
  e["Variables"][i]["Lower Bound"] = float(obs_low)
  e["Variables"][i]["Upper Bound"] = float(obs_upper)

  
 # Defining Action Variables
 
 for i in range(actionVariableCount):
  e["Variables"][stateVariableCount + i]["Name"] = "Action Variable " + str(i)
  e["Variables"][stateVariableCount + i]["Type"] = "Action"
  e["Variables"][stateVariableCount + i]["Lower Bound"] = float(ac_low)
  e["Variables"][stateVariableCount + i]["Upper Bound"] = float(ac_upper)
  e["Variables"][stateVariableCount + i]["Initial Exploration Noise"] = math.sqrt(0.2)
 
 ### Defining Termination Criteria

 e["Solver"]["Termination Criteria"]["Testing"]["Target Average Reward"] = math.inf
 

def agent(s, env, model = ''):


 if (s["Custom Settings"]["Print Step Information"] == "Enabled"):
  printStep = True
 else:
  printStep = False
 
 env.reset()
 
 states = []
 
 if (env.env.env.metadata['name']== 'waterworld_v3') and model == '' :

   for ag in env.agents:
      state = env.observe(ag).tolist()
      states.extend(state)

 elif (env.env.env.metadata['name']== 'waterworld_v3') and model == '1' :
   global_state = []
   for ag in env.agents:
      state = env.observe(ag).tolist()
      global_state.extend(state)

   for ag in env.agents:
      state = env.observe(ag).tolist()
      state.extend(global_state)
      states.append(state)
 elif (env.env.env.metadata['name']== 'waterworld_v3') and model == '2' :
   states = []
   name = 'pursuer_'
   for ag in env.agents:
      state = env.observe(ag).tolist()
      i = int(ag[-1])
      temp = np.arange(5)
      temp = np.delete(temp, np.where(temp == i))
      temp = np.random.permutation(temp)
      for k in temp:
         temp_name = name + str(k)
         state.extend(env.observe(temp_name).tolist())
      states.append(state)

    

 s["State"] = states
 
 step = 0
 done = False

 # Storage for cumulative reward
 cumulativeReward = 0.0
 
 overSteps = 0
 if s["Mode"] == "Testing":
   image_count = 0

  
 while not done and step < 500:

  s.update()
  
  # Printing step information    
  if (printStep):  print('[Korali] Frame ' + str(step), end = '')
  
  
  actions = s["Action"]
  rewards = []
  
  for ag in env.agents:
   if s["Mode"] == "Testing" and (env.env.env.metadata['name']== 'waterworld_v3'):
      obs=env.env.env.env.render('rgb_array')
      im = Image.fromarray(obs)
      fname = os.path.join("/scratch/mzeqiri/korali/examples/study.cases/pettingZoo/images/","image_{0}.png".format(image_count))
      im.save(fname)
      image_count += 1
      

   observation, reward, done, info = env.last()
   rewards.append(reward)

   if (env.env.env.metadata['name']== 'waterworld_v3') and model == '' :
      action = []
      action.append(actions.pop(0))
      action.append(actions.pop(0))
   elif (env.env.env.metadata['name']== 'waterworld_v3') and (model == '1' or model == '2') :
      action = actions.pop(0)
   
   if done and (env.env.env.metadata['name']== 'multiwalker_v7'):
    continue
   env.step(np.array(action,dtype= 'float32'))
   


  # Getting Reward
  if (env.env.env.metadata['name']== 'waterworld_v3') and model == '' :
   s["Reward"] = rewards[-1]
  elif (env.env.env.metadata['name']== 'waterworld_v3') and (model == '1' or model == '2') :
   s["Reward"] = rewards



  
  # Storing New State
  states = []
  if (env.env.env.metadata['name']== 'waterworld_v3') and model == '' :
   for ag in env.agents:
      state = env.observe(ag).tolist()
      states.extend(state)

  elif (env.env.env.metadata['name']== 'waterworld_v3') and model == '1' :
   global_state = []
   for ag in env.agents:
      state = env.observe(ag).tolist()
      global_state.extend(state)
   for ag in env.agents:
      state = env.observe(ag).tolist()
      state.extend(global_state)
      states.append(state)  
  
  elif (env.env.env.metadata['name']== 'waterworld_v3') and model == '2' :
   states = []
   name = 'pursuer_'
   for ag in env.agents:
      state = env.observe(ag).tolist()
      i = int(ag[-1])
      temp = np.arange(5)
      temp = np.delete(temp, np.where(temp == i))
      temp = np.random.permutation(temp)
      for k in temp:
         temp_name = name + str(k)
         state.extend(env.observe(temp_name).tolist())
      states.append(state)
  
  s["State"] = states
  # Advancing step counter
  step = step + 1

 # Setting termination status
 if (not env.agents):
  s["Termination"] = "Terminal"
 else:
  s["Termination"] = "Truncated"

 if s["Mode"] == "Testing":
   env.close()
