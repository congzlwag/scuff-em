/* Copyright (C) 2005-2011 M. T. Homer Reid
 *
 * This file is part of SCUFF-EM.
 *
 * SCUFF-EM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SCUFF-EM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * AssembleBEMMatrix.cc -- libscuff routine for assembling a single 
 *                      -- block of the BEM matrix (i.e. the       
 *                      -- interactions of two objects in the geometry)
 *                      --
 *                      -- (cf. 'libscuff Implementation and Technical
 *                      --  Details', section 8.3, 'Structure of the BEM
 *                      --  Matrix.')
 *                      --
 * homer reid           -- 10/2006 -- 6/2012
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libhmat.h>
#include <libhrutil.h>
#include <omp.h>

#include "libscuff.h"
#include "libscuffInternals.h"

#include "cmatheval.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#ifdef USE_PTHREAD
#  include <pthread.h>
#endif

namespace scuff {

#define II cdouble(0,1)

/***************************************************************/
/***************************************************************/
/***************************************************************/
typedef struct ThreadData
 { 
   AOCMBArgStruct *Args;
   int nt, nThread;

 } ThreadData;

/***************************************************************/
/***************************************************************/
/***************************************************************/
void *ACCMBThread(void *data)
{
  /***************************************************************/
  /* local copies of fields in argument structure                */
  /***************************************************************/
  ThreadData *TD = (ThreadData *)data;
  AOCMBArgStruct *Args  = TD->Args;
  //RWGGeometry *G        = Args->G;
  RWGComposite *CA      = Args->CA;
  RWGComposite *CB      = Args->CB;
  cdouble Omega         = Args->Omega;
  HMatrix *M            = Args->M;
  int RowOffset         = Args->RowOffset;
  int ColOffset         = Args->ColOffset;

  /***************************************************************/
  /* other local variables ***************************************/
  /***************************************************************/
  int npsa, NPSA = CA->NumPartialSurfaces;
  int npsb, NPSB = CB->NumPartialSurfaces;
  int ntea, NTEA;
  int nteb, NTEB; 

  int SubRegionA1, SubRegionA2, SubRegionB1, SubRegionB2;
  int CommonSubRegion[2], NumCommonSubRegions;

  cdouble Eps1, Mu1, k1, EEPreFac1, MEPreFac1, MMPreFac1;
  cdouble Eps2, Mu2, k2, EEPreFac2, MEPreFac2, MMPreFac2;

  cdouble GC[2];

  for(npsa=0; npsa<NPSA; npsa++)
   for(npsb=0; npsb<NPSB; npsb++)
    { 
      /*--------------------------------------------------------------*/
      /* figure out if partial surfaces #npsa and #npsb share 0, 1,   */
      /* or 2 subregions in common. if the answer is 0 then basis     */
      /* functions on these two partial surfaces do not interact.     */
      /*--------------------------------------------------------------*/
      SubRegionA1 = SubRegions[2*npsa+0];
      SubRegionA2 = SubRegions[2*npsa+1];
      SubRegionB1 = SubRegions[2*npsb+0];
      SubRegionB2 = SubRegions[2*npsb+1];
      NumCommonSubRegions=0;
      if ( SubRegionA1==SubRegionB1 || SubRegionA1==SubRegionB2 )
       CommonSubRegion[NumCommonSubRegions++]=SubRegionA1;
      if ( SubRegionA2==SubRegionB1 || SubRegionA2==SubRegionB2 )
       CommonSubRegion[NumCommonSubRegions++]=SubRegionA2;
      if (NumCommonSubRegions==0) 
       continue;

      /*--------------------------------------------------------------*/
      /* if the two RWG composites in question are distinct ... ------*/
      /*--------------------------------------------------------------*/
      if ( (CA!=CB) )
       ErrExit("%s:%i: feature not yet implemented",__FILE__,__LINE__);

      /*--------------------------------------------------------------*/
      /*--------------------------------------------------------------*/
      /*--------------------------------------------------------------*/
      Eps1=EpsTF[ CommonSubRegion[0] ];
      Mu1=EpsTF[ CommonSubRegion[0] ];
      k1=csqrt2(Eps1*Mu1)*Omega;
      EEPreFac1 = Sign*II*Mu1*Omega;
      EMPreFac1 = -Sign*II*k1;
      MMPreFac1 = -1.0*Sign*II*Eps1*Omega;

      if (NumCommonSubRegions==2)
       { Eps2=EpsTF[ CommonSubRegion[1] ]; 
         Mu2=EpsTF[ CommonSubRegion[1] ]; }
         k2=csqrt2(Eps2*Mu2)*Omega;
         EEPreFac2 = Sign*II*Mu2*Omega;
         EMPreFac2 = -Sign*II*k2;
         MMPreFac2 = -1.0*Sign*II*Eps2*Omega;
       };

      /***************************************************************/
      /* preinitialize an argument structure to be passed to         */
      /* GetEdgeEdgeInteractions() below                             */
      /***************************************************************/
      GetEEIArgStruct MyGetEEIArgs, *GetEEIArgs=&MyGetEEIArgs;
      InitGetEEIArgs(GetEEIArgs);

      GetEEIArgs->PanelsA=OSA->Panels;
      GetEEIArgs->VerticesA=CA->Vertices;
      GetEEIArgs->PanelsB=OSB->Panels;
      GetEEIArgs->VerticesB=CB->Vertices;
      GetEEIArgs->NumGradientComponents = GradB ? 3 : 0;
      GetEEIArgs->NumTorqueAxes=NumTorqueAxes;
      GetEEIArgs->GammaMatrix=GammaMatrix;
    
      /* pointers to arrays inside the structure */
      cdouble *GC=GetEEIArgs->GC;
      cdouble *GradGC=GetEEIArgs->GradGC;
      cdouble *dGCdT=GetEEIArgs->dGCdT;
          
      /*--------------------------------------------------------------*/
      /* now loop over all basis functions (both full and half RWG    */
      /* functions) on partial surfaces #npsa and #npsb.              */
      /*--------------------------------------------------------------*/
      NTEA=PartialSurfaces[npsa]->NumTotalEdges;
      OffsetA = RowOffset + BFIndexOffset[npsa];
      NTEB=PartialSurfaces[npsb]->NumTotalEdges;
      OffsetB = ColOffset + BFIndexOffset[npsb];
      for(ntea=0; ntea<NTEA; ntea++)
       for(nteb=Symmetric*ntea; nteb<NTEB; nteb++)
        { 
          nt++;
          if (nt==TD->nThread) nt=0;
          if (nt!=TD->nt) continue;

       //   if (G->LogLevel>=SCUFF_VERBOSELOGGING)
           for(int PerCent=0; PerCent<9; PerCent++)
            if ( nteb==Symmetric*ntea &&  (ntea == (PerCent*NTEA)/10) )
             MutexLog("%i0 %% (%i/%i)...",PerCent,ntea,NTEA);
          
          GetEEIArgs->k=k1;
          GetEEIArgs->Ea=PartialSurfaces[npsa]->Edges[ntea];
          GetEEIArgs->Eb=PartialSurfaces[npsa]->Edges[nteb];
          GetEdgeEdgeInteractions(&GetEEIArgs);

          B->SetEntry(OffsetA + 2*ntea+0, OffsetB + 2*nteb+0, EEPreFac1*GC[0]);
          B->SetEntry(OffsetA + 2*ntea+0, OffsetB + 2*nteb+1, EMPreFac1*GC[1]);
          B->SetEntry(OffsetA + 2*ntea+1, OffsetB + 2*nteb+0, EMPreFac1*GC[1]);
          B->SetEntry(OffsetA + 2*ntea+1, OffsetB + 2*nteb+1, MMPreFac1*GC[0]);
          
          if (NumCommonSubRegions==2)
           { GetEEIArgs->k=k2;
             GetEdgeEdgeInteractions(&GetEEIArgs);
             B->AddEntry(OffsetA + 2*ntea+0, OffsetB + 2*nteb+0, EEPreFac2*GC[0]);
             B->AddEntry(OffsetA + 2*ntea+0, OffsetB + 2*nteb+1, EMPreFac2*GC[1]);
             B->AddEntry(OffsetA + 2*ntea+1, OffsetB + 2*nteb+0, EMPreFac2*GC[1]);
             B->AddEntry(OffsetA + 2*ntea+1, OffsetB + 2*nteb+1, MMPreFac2*GC[0]);
           };
  
        };

    }; // for (npsa=0 ...) for npsb=0 ...)
}

/***************************************************************/
/* this is a version of the AssembleBEMMatrixBlock() routine   */
/* for use when one (or both) of the two objects in question   */
/* is an RWGComposite instead of an RWGObject.                 */
/***************************************************************/
/***************************************************************/  
/***************************************************************/  
/***************************************************************/
void AssembleCCMatrixBlock(ACCMBArgStruct *Args)
{ 
  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  //RWGGeometry  *G=Args->G;
  RWGComposite *CA=Args->CA;
  RWGComposite *CB=Args->CB;
  cdouble Omega=Args->Omega;

  /***************************************************************/
  /* precompute material properties of all regions on both       */
  /* composites                                                  */
  /***************************************************************/
  int nsr;
  for(nsr=0; nsr<CA->NumSubRegions; nsr++)
   CA->SubRegionMPs[nsr] -> GetEpsMu(Omega, CA->EpsTF + nsr, CA->MuTF + nsr);
  if (CB!=CA)
   { for(nsr=0; nr<CB->NumSubRegions; nsr++)
      CB->SubRegionMPs[nsr] -> GetEpsMu(Omega, CB->EpsTF + nsr, CB->MuTF + nsr);
   };

  /***************************************************************/
  /* fire off threads ********************************************/
  /***************************************************************/
  GlobalFIPPICache.Hits=GlobalFIPPICache.Misses=0;

  int nt, nThread=Args->nThread;

#ifdef USE_PTHREAD
  ThreadData *TDs = new ThreadData[nThread], *TD;
  pthread_t *Threads = new pthread_t[nThread];
  for(nt=0; nt<nThread; nt++)
   { 
     TD=&(TDs[nt]);
     TD->nt=nt;
     TD->nThread=nThread;
     TD->Args=Args;
     if (nt+1 == nThread)
       ACCMBThread((void *)TD);
     else
       pthread_create( &(Threads[nt]), 0, ACCMBThread, (void *)TD);
   }
  for(nt=0; nt<nThread-1; nt++)
   pthread_join(Threads[nt],0);
  delete[] Threads;
  delete[] TDs;

#else 
#ifndef USE_OPENMP
  nThread=1;
#else
#pragma omp parallel for schedule(dynamic,1), num_threads(nThread)
#endif
  for(nt=0; nt<nThread*100; nt++)
   { 
     ThreadData TD1;
     TD1.nt=nt;
     TD1.nThread=nThread*100;
     TD1.Args=Args;
     ACCMBThread((void *)&TD1);
   };
#endif


 // if (G->LogLevel>=SCUFF_VERBOSELOGGING)
   Log("  %i/%i cache hits/misses",GlobalFIPPICache.Hits,GlobalFIPPICache.Misses);

}
