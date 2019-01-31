/* --------------------------------------------------------- */
/* --- File: cmaes.h ----------- Author: Nikolaus Hansen --- */
/* ---------------------- last modified: IX 2010         --- */
/* --------------------------------- by: Nikolaus Hansen --- */
/* --------------------------------------------------------- */
/*   
     CMA-ES for non-linear function minimization. 

     Copyright (C) 1996, 2003-2010  Nikolaus Hansen. 
     e-mail: nikolaus.hansen (you know what) inria.fr
      
     License: see file cmaes.c
   
*/
#ifndef CMAES_TYPES_H
#define CMAES_TYPES_H

#include <time.h>
#include "korali.h"

typedef struct 
/* cmaes_random_t 
 * sets up a pseudo random number generator instance 
 */
{
  /* Variables for Uniform() */
  long int aktseed;
  long int aktrand;
  long int *rgrand;
  
  /* Variables for Gauss() */
  short flgstored;
  double hold;
} cmaes_random_t;

typedef struct 
/* cmaes_timings_t 
 * time measurement, used to time eigendecomposition 
 */
{
  /* for outside use */
  double totaltime; /* zeroed by calling re-calling cmaes_timings_start */
  double totaltotaltime;
  double tictoctime; 
  double lasttictoctime;
  
  /* local fields */
  clock_t lastclock;
  time_t lasttime;
  clock_t ticclock;
  time_t tictime;
  short istic;
  short isstarted; 

  double lastdiff;
  double tictoczwischensumme;
} cmaes_timings_t;

typedef struct 
/* cmaes_readpara_t
 * collects all parameters, in particular those that are read from 
 * a file before to start. This should split in future? 
 */
{
  char * filename;  /* keep record of the file that was taken to read parameters */
  short flgsupplemented; 
  
  /* input parameters */
  double * rgDiffMinChange; 

  /* termination parameters */
  struct { int flg; double val; } stStopFitness; 
  double stopTolFunHist;

  /* internal evolution strategy parameters */
  double ccov;         /* <- mucov, <- N */
  double diagonalCov;  /* number of initial iterations */
  struct { int flgalways; double modulo; double maxtime; } updateCmode;
  double facupdateCmode;

  /* supplementary variables */

  char *weigkey; 
  char resumefile[99];
  const char **rgsformat;
  void **rgpadr;
  const char **rgskeyar;
  double ***rgp2adr;
  int n1para, n1outpara;
  int n2para;
} cmaes_readpara_t;

typedef struct 
/* cmaes_t 
 * CMA-ES "object" 
 */
{
  const char *version;
  /* char *signalsFilename; */
  cmaes_readpara_t sp;
  cmaes_random_t rand; /* random number generator */

  double sigma;  /* step size */

  double *rgxmean;  /* mean x vector, "parent" */
  double *rgxbestever; 
  double **rgrgx;   /* range of x-vectors, lambda offspring */
  int *index;       /* sorting index of sample pop. */
  double *arFuncValueHist;

  short flgIniphase; /* not really in use anymore */
  short flgStop; 

  double chiN; 
  double **C;  /* lower triangular matrix: i>=j for C[i][j] */
  double **B;  /* matrix with normalize eigenvectors in columns */
  double *rgD; /* axis lengths */

  double *rgpc;
  double *rgps;
  double *rgxold; 
  double *rgout; 
  double *rgBDz;   /* for B*D*z */
  double *rgdTmp;  /* temporary (random) vector used in different places */
  double *rgFuncValue; 
  double *publicFitness; /* returned by cmaes_init() */

  double gen; /* Generation number */
  double countevals;
  double state; /* 1 == sampled, 2 == not in use anymore, 3 == updated */

  double maxdiagC; /* repeatedly used for output */
  double mindiagC;
  double maxEW;
  double minEW;

  char sOutString[330]; /* 4x80 */

  short flgEigensysIsUptodate;
  short flgCheckEigen; /* control via cmaes_signals.par */
  double genOfEigensysUpdate; 
  cmaes_timings_t eigenTimings;
 
  double dMaxSignifKond; 				     
  double dLastMinEWgroesserNull;

  short flgresumedone; 

  time_t printtime; 
  time_t writetime; /* ideally should keep track for each output file */
  time_t firstwritetime;
  time_t firstprinttime; 

} cmaes_t; 



/*
  storage for distribution parameters:
  mean mu
  Covariant matrix C decomposed as:
  - C = Q D Q^{-1} if !flgdiag
  - C = D          if  flgdiag
  only diagonal elements are stored in D 
 */

typedef struct {
    int dim, flgdiag;
    double **Q;
    double *D;
    double *mu;
    double *w; // workspace
} cmaes_distr_t;

#endif // CMAES_TYPES_H
