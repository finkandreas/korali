#!/usr/bin/env python3

##  This code was inspired by the OpenAI Gym Mountaincar-v0 environment

## Track described by function x^2

##  Copyright (c) 2018 CSE-Lab, ETH Zurich, Switzerland. All rights reserved.
##  Distributed under the terms of the MIT license.

import math
import numpy as np, sys
from scipy.integrate import ode

class MountainCart:
  def __init__(self):
    self.dt = 0.1
    self.ddt = 0.001
    self.m = 1.0 # mass
    self.g = 9.81 # gravitaty
    self.r = 0.1 # friction
    self.maxForce = 1.0 # max absolute force
    self.maxSteps = 1000
    
    self.step = 0
    self.t = 0
    
    self.F = 0.0 # force in cart direction (facing right)
    self.u = np.asarray([0, 0, 0, 0, 0, 0]) # location x, location y, velocity x, velocity y, acceleration x, acceleration y
    self.highest = 0

    assert(self.g > self.maxForce/self.m) # avoid weird configuration
    
  def reset(self, seed):
    np.random.seed(seed)
  
    # for recording
    self.actions = np.zeros(self.maxSteps)
    self.locations = np.zeros((2,self.maxSteps))
   
    self.fgravity = np.zeros(self.maxSteps)
    self.velocity = np.zeros((2,self.maxSteps))
    self.acceleration = np.zeros((2,self.maxSteps))
    
    self.step = 0
    self.t = 0
 
    self.F = 0
    self.u = np.random.uniform(-0.05, 0.05, 6)
    self.highest = 0
    
    slope = 2.*self.u[0] # track is x^2
    theta = np.arctan(slope)
    fg = -np.sin(theta)*self.m*self.g
    fxg = np.cos(theta)*fg
    fyg = np.sin(theta)*fg
    self.u[4] = fxg
    self.u[5] = fyg

  def isFailed(self):
    return False

  def isOver(self):
    return self.isFailed()

  def advance(self, action):
    self.F = np.clip(action[0], a_min=-self.maxForce, a_max=+self.maxForce)
    
    # simulate dt seconds 
    for i in range(int(self.dt/self.ddt)):
       
        # calculate acceleration 
        slope = 2.*self.u[0] # "ground" is x^2
        theta = np.arctan(slope)
        fg = -self.m*self.g
        fx = self.F*np.cos(theta)
        fy = self.F*np.sin(theta)
        ftot = np.array([fx, fy+fg])
        normal = np.array([-slope, 1])
        normal /= np.linalg.norm(normal)

        fnormal = normal*np.dot(ftot, normal)/np.dot(normal, normal)
        fres = ftot-fnormal
	        
        #print(slope)
        #print(ftot)
        #print(normal)
        #print(fnormal)
        #print(fres)

        # update acceleration
        self.u[4] = fres[0]/self.m
        self.u[5] = fres[1]/self.m
  
        # leapfrog scheme (location)
        self.u[0] = self.u[0] + self.u[2]*self.ddt + 0.5*self.u[4]*self.ddt**2
        self.u[1] = self.u[0]**2 # track is x^2 # self.u[1] + self.u[3]*self.ddt + 0.5*self.u[5]*self.ddt**2
        
        # leapfrog scheme (velocity part a)
        self.u[2] = self.u[2] + 0.5*self.u[4]*self.ddt
        self.u[3] = self.u[3] + 0.5*self.u[5]*self.ddt
  
    
        # calculate acceleration at new position
        slope = 2.*self.u[0] # "ground" is x^2
        theta = np.arctan(slope)
        fg = -self.m*self.g
        fx = self.F*np.cos(theta)
        fy = self.F*np.sin(theta)
        ftot = np.array([fx, fy+fg])
        normal = np.array([-slope, 1])
        normal /= np.linalg.norm(normal)

        fnormal = normal*np.dot(ftot, normal)/np.dot(normal, normal)
        fres = ftot-fnormal
	
        # update acceleration
        self.u[4] = fres[0]/self.m
        self.u[5] = fres[1]/self.m

        # leapfrog scheme (velocity part b)
        self.u[2] = self.u[2] + 0.5*self.u[4]*self.ddt
        self.u[3] = self.u[3] + 0.5*self.u[5]*self.ddt
  
        #print(self.u[:4])
        if self.u[0] > 0:
            assert(self.u[4]*self.u[5] > 0)
        else:
            assert(self.u[4]*self.u[5] < 0)

       
        self.t = self.t + self.ddt
    
    self.actions[self.step] = self.F
    self.locations[:, self.step] = self.u[:2]
    self.fgravity[self.step] = fg
    self.velocity[:, self.step] = self.u[2:4]
    self.acceleration[:, self.step] = self.u[4:]

    self.step = self.step + 1
    
    if self.isOver(): 
      return 1
    else: 
      return 0

  def getState(self):
    state = self.u.copy()
    return state

  def getReward(self):
    if self.u[1] > self.highest:
        self.highest = self.u[1]
        return self.highest
    else:
        return 0


if __name__=="__main__":
    cart = MountainCart()
    maxSteps = 1000
    cart.reset(0xC0FF33)
 
    step = 0

    sumofrewards = 0
    while step < maxSteps:

      # Performing the action
      cart.advance([-10])
      
      # Getting Reward
      reward = cart.getReward()
       
      # Storing New State
      state = cart.getState().tolist()
   
      print("Step {}".format(step))
      print("State {}, Reward {}".format(state, reward))
 
      sumofrewards += reward
      # Advancing step counter
      step = step + 1
 
    print("Sum Of Rewards {}".format(sumofrewards))
