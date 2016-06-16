/**CFile****************************************************************

  FileName    [giaSatLE.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Scalable AIG package.]

  Synopsis    [Mapping with edges.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: giaSatLE.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "gia.h"
#include "misc/extra/extra.h"
#include "misc/tim/tim.h"
#include "sat/bsat/satStore.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

static inline int   Sle_CutSize( int * pCut )          { return pCut[0] & 0xF;  }  //  4 bits
static inline int   Sle_CutSign( int * pCut )          { return pCut[0] >> 4;   }  // 28 bits
static inline int   Sle_CutSetSizeSign( int s, int S ) { return (S << 28) | s;  }
static inline int * Sle_CutLeaves( int * pCut )        { return pCut + 1;       } 

static inline int   Sle_CutIsUsed( int * pCut )        { return pCut[1] != 0;   }
static inline void  Sle_CutSetUnused( int * pCut )     { pCut[1] = 0;           }

#define Sle_ForEachCut( pList, pCut, i ) for ( i = 0, pCut = pList + 1; i < pList[0]; i++, pCut += Sle_CutSize(pCut) + 1 )

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Cut computation.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline int Sle_CutMergeOrder( int * pCut0, int * pCut1, int * pCut, int nLutSize )
{ 
    int nSize0   = Sle_CutSize(pCut0);
    int nSize1   = Sle_CutSize(pCut1);
    int i, * pC0 = Sle_CutLeaves(pCut0);
    int k, * pC1 = Sle_CutLeaves(pCut1);
    int c, * pC  = Sle_CutLeaves(pCut);
    // the case of the largest cut sizes
    if ( nSize0 == nLutSize && nSize1 == nLutSize )
    {
        for ( i = 0; i < nSize0; i++ )
        {
            if ( pC0[i] != pC1[i] )  return 0;
            pC[i] = pC0[i];
        }
        pCut[0] = Sle_CutSetSizeSign( nLutSize, Sle_CutSign(pCut0) | Sle_CutSign(pCut1) );
        return 1;
    }
    // compare two cuts with different numbers
    i = k = c = 0;
    if ( nSize0 == 0 ) goto FlushCut1;
    if ( nSize1 == 0 ) goto FlushCut0;
    while ( 1 )
    {
        if ( c == nLutSize ) return 0;
        if ( pC0[i] < pC1[k] )
        {
            pC[c++] = pC0[i++];
            if ( i >= nSize0 ) goto FlushCut1;
        }
        else if ( pC0[i] > pC1[k] )
        {
            pC[c++] = pC1[k++];
            if ( k >= nSize1 ) goto FlushCut0;
        }
        else
        {
            pC[c++] = pC0[i++]; k++;
            if ( i >= nSize0 ) goto FlushCut1;
            if ( k >= nSize1 ) goto FlushCut0;
        }
    }

FlushCut0:
    if ( c + nSize0 > nLutSize + i ) return 0;
    while ( i < nSize0 )
        pC[c++] = pC0[i++];
    pCut[0] = Sle_CutSetSizeSign( c, Sle_CutSign(pCut0) | Sle_CutSign(pCut1) );
    return 1;

FlushCut1:
    if ( c + nSize1 > nLutSize + k ) return 0;
    while ( k < nSize1 )
        pC[c++] = pC1[k++];
    pCut[0] = Sle_CutSetSizeSign( c, Sle_CutSign(pCut0) | Sle_CutSign(pCut1) );
    return 1;
}
static inline int Sle_SetCutIsContainedOrder( int * pBase, int * pCut ) // check if pCut is contained in pBase
{
    int i, nSizeB = Sle_CutSize(pBase);
    int k, nSizeC = Sle_CutSize(pCut);
    int * pLeaveB = Sle_CutLeaves(pBase);
    int * pLeaveC = Sle_CutLeaves(pCut);
    if ( nSizeB == nSizeC )
    {
        for ( i = 0; i < nSizeB; i++ )
            if ( pLeaveB[i] != pLeaveC[i] )
                return 0;
        return 1;
    }
    assert( nSizeB > nSizeC ); 
    if ( nSizeC == 0 )
        return 1;
    for ( i = k = 0; i < nSizeB; i++ )
    {
        if ( pLeaveB[i] > pLeaveC[k] )
            return 0;
        if ( pLeaveB[i] == pLeaveC[k] )
        {
            if ( ++k == nSizeC )
                return 1;
        }
    }
    return 0;
}
static inline int Sle_CutCountBits( unsigned i )
{
    i = i - ((i >> 1) & 0x55555555);
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
    i = ((i + (i >> 4)) & 0x0F0F0F0F);
    return (i*(0x01010101))>>24;
}
static inline int Sle_SetLastCutIsContained( Vec_Int_t * vTemp, int * pBase )
{
    int i, * pCut, * pList = Vec_IntArray(vTemp);
    Sle_ForEachCut( pList, pCut, i )
        if ( Sle_CutIsUsed(pCut) && Sle_CutSize(pCut) <= Sle_CutSize(pBase) && (Sle_CutSign(pCut) & Sle_CutSign(pBase)) == Sle_CutSign(pCut) && Sle_SetCutIsContainedOrder(pBase, pCut) )
            return 1;
    return 0;
}
static inline void Sle_SetAddCut( Vec_Int_t * vTemp, int * pCut )
{
    int i, * pBase, * pList = Vec_IntArray(vTemp);
    Sle_ForEachCut( pList, pBase, i )
        if ( Sle_CutIsUsed(pBase) && Sle_CutSize(pCut) < Sle_CutSize(pBase) && (Sle_CutSign(pCut) & Sle_CutSign(pBase)) == Sle_CutSign(pCut) && Sle_SetCutIsContainedOrder(pBase, pCut) )
            Sle_CutSetUnused( pBase );
    Vec_IntPushArray( vTemp, pCut, Sle_CutSize(pCut)+1 );
    Vec_IntAddToEntry( vTemp, 0, 1 );
}
int Sle_ManCutMerge( Gia_Man_t * p, int iObj, Vec_Int_t * vCuts, Vec_Int_t * vTemp, int nLutSize )
{
    Gia_Obj_t * pObj = Gia_ManObj( p, iObj );
    int * pList0 = Vec_IntEntryP( vCuts, Vec_IntEntry(vCuts, Gia_ObjFaninId0(pObj, iObj)) );
    int * pList1 = Vec_IntEntryP( vCuts, Vec_IntEntry(vCuts, Gia_ObjFaninId1(pObj, iObj)) );
    int * pCut0, * pCut1, i, k, Cut[8], nCuts = 0;
    Vec_IntFill( vTemp, 1, 0 );
    Sle_ForEachCut( pList0, pCut0, i )
    Sle_ForEachCut( pList1, pCut1, k )
    {
        if ( Sle_CutSize(pCut0) + Sle_CutSize(pCut1) > nLutSize && Sle_CutCountBits(Sle_CutSign(pCut0) | Sle_CutSign(pCut1)) > nLutSize )
            continue;
        if ( !Sle_CutMergeOrder(pCut0, pCut1, Cut, nLutSize) )
            continue;
        if ( Sle_SetLastCutIsContained(vTemp, Cut) )
            continue;
        Sle_SetAddCut( vTemp, Cut );
    }
    // reload
    Vec_IntWriteEntry( vCuts, iObj, Vec_IntSize(vCuts) );
    Vec_IntPush( vCuts, -1 );
    pList0 = Vec_IntArray(vTemp);
    Sle_ForEachCut( pList0, pCut0, i )
    {
        if ( !Sle_CutIsUsed(pCut0) )
            continue;
        Vec_IntPushArray( vCuts, pCut0, Sle_CutSize(pCut0)+1 );
        nCuts++;
    }
    Vec_IntPush( vCuts, Sle_CutSetSizeSign(1, iObj % 28) );
    Vec_IntPush( vCuts, iObj );
    Vec_IntWriteEntry( vCuts, Vec_IntEntry(vCuts, iObj), nCuts+1 );
    return nCuts;
}
Vec_Int_t * Sle_ManComputeCuts( Gia_Man_t * p, int nLutSize, int fVerbose )
{
    int i, iObj, nCuts = 0;
    Vec_Int_t * vTemp = Vec_IntAlloc( 1000 );
    Vec_Int_t * vCuts = Vec_IntAlloc( 30 * Gia_ManAndNum(p) );
    assert( nLutSize <= 6 );
    Vec_IntFill( vCuts, Gia_ManObjNum(p), 0 );
    Gia_ManForEachCiId( p, iObj, i )
    {
        Vec_IntWriteEntry( vCuts, iObj, Vec_IntSize(vCuts) );
        Vec_IntPush( vCuts, 1 );
        Vec_IntPush( vCuts, Sle_CutSetSizeSign(1, iObj % 28) );
        Vec_IntPush( vCuts, iObj );
    }
    Gia_ManForEachAndId( p, iObj )
        nCuts += Sle_ManCutMerge( p, iObj, vCuts, vTemp, nLutSize );
    if ( fVerbose )
        printf( "Nodes = %d.  Cuts = %d.  Cuts/Node = %.2f.  Ints/Node = %.2f.  Mem = %.2f MB.\n", 
            Gia_ManAndNum(p), nCuts, 1.0*nCuts/Gia_ManAndNum(p), 
            1.0*(Vec_IntSize(vCuts)-Gia_ManObjNum(p))/Gia_ManAndNum(p), 
            1.0*Vec_IntMemory(vCuts) / (1<<20) );
    Vec_IntFree( vTemp );
    return vCuts;
}
void Sle_ManPrintCut( int * pCut )
{
    int nSize   = Sle_CutSize(pCut);
    int k, * pC = Sle_CutLeaves(pCut);
    printf( "{" );
    for ( k = 0; k < nSize; k++ )
        printf( " %d", pC[k] );
    printf( " }\n" );
}
void Sle_ManPrintCuts( Gia_Man_t * p, Vec_Int_t * vCuts, int iObj )
{
    int i, * pCut;
    int * pList = Vec_IntEntryP( vCuts, Vec_IntEntry(vCuts, iObj) );
    printf( "Obj %3d\n", iObj );
    Sle_ForEachCut( pList, pCut, i )
        Sle_ManPrintCut( pCut );
    printf( "\n" );
}
void Sle_ManPrintCutsAll( Gia_Man_t * p, Vec_Int_t * vCuts )
{
    int iObj;
    Gia_ManForEachAndId( p, iObj )
        Sle_ManPrintCuts( p, vCuts, iObj );
}
void Sle_ManComputeCutsTest( Gia_Man_t * p )
{
    Vec_Int_t * vCuts = Sle_ManComputeCuts( p, 4, 1 );
    //Sle_ManPrintCutsAll( p, vCuts );
    Vec_IntFree( vCuts );
}



/**Function*************************************************************

  Synopsis    [Derive mask representing internal nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Bit_t * Sle_ManInternalNodeMask( Gia_Man_t * pGia )
{
    int iObj;
    Vec_Bit_t * vMask = Vec_BitStart( Gia_ManObjNum(pGia) );  
    Gia_ManForEachAndId( pGia, iObj )
        Vec_BitWriteEntry( vMask, iObj, 1 );
    return vMask;
}

/**Function*************************************************************

  Synopsis    [Derive cut fanins of each node.]

  Description [These are nodes that are fanins of some cut of this node.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sle_ManCollectCutFaninsOne( Gia_Man_t * pGia, int iObj, Vec_Int_t * vCuts, Vec_Bit_t * vMask, Vec_Int_t * vCutFanins, Vec_Bit_t * vMap )
{
    int i, iFanin, * pCut, * pList = Vec_IntEntryP( vCuts, Vec_IntEntry(vCuts, iObj) );
    Sle_ForEachCut( pList, pCut, i )
    {
        int nSize   = Sle_CutSize(pCut);
        int k, * pC = Sle_CutLeaves(pCut);
        if ( nSize < 2 )
            continue;
        for ( k = 0; k < nSize; k++ )
            if ( Vec_BitEntry(vMask, pC[k]) && !Vec_BitEntry(vMap, pC[k]) )
            {
                Vec_BitWriteEntry( vMap, pC[k], 1 );
                Vec_IntPush( vCutFanins, pC[k] );
            }
    }
    Vec_IntForEachEntry( vCutFanins, iFanin, i )
        Vec_BitWriteEntry( vMap, iFanin, 0 );
}
Vec_Wec_t * Sle_ManCollectCutFanins( Gia_Man_t * pGia, Vec_Int_t * vCuts, Vec_Bit_t * vMask )
{
    int iObj;
    Vec_Bit_t * vMap = Vec_BitStart( Gia_ManObjNum(pGia) );
    Vec_Wec_t * vCutFanins = Vec_WecStart( Gia_ManObjNum(pGia) );
    Gia_ManForEachAndId( pGia, iObj )
        Sle_ManCollectCutFaninsOne( pGia, iObj, vCuts, vMask, Vec_WecEntry(vCutFanins, iObj), vMap );
    Vec_BitFree( vMap );
    return vCutFanins;
}


typedef struct Sle_Man_t_ Sle_Man_t;
struct Sle_Man_t_
{
    // user's data
    Gia_Man_t *    pGia;         // user's manager (with mapping and edges)
    int            nLevels;      // total number of levels
    int            fVerbose;     // verbose flag
    // SAT variables
    int            nNodeVars;    // node variables (Gia_ManAndNum(pGia))
    int            nCutVars;     // cut variables (total number of non-trivial cuts)
    int            nEdgeVars;    // edge variables (total number of internal edges)
    int            nDelayVars;   // delay variables (nNodeVars * nLevelsMax)
    int            nVarsTotal;   // total number of variables
    // SAT clauses
    int            nCutClas;     // cut clauses
    int            nEdgeClas;    // edge clauses
    int            nEdgeClas2;   // edge clauses exclusivity
    int            nDelayClas;   // delay clauses
    // internal data
    sat_solver *   pSat;         // SAT solver
    Vec_Bit_t *    vMask;        // internal node mask
    Vec_Int_t *    vCuts;        // cuts for each node
    Vec_Wec_t *    vCutFanins;   // internal cut fanins of each node
    Vec_Wec_t *    vFanoutEdges; // internal cut fanins of each node
    Vec_Wec_t *    vEdgeCuts;    // cuts of each edge for one node
    Vec_Int_t *    vObjMap;      // temporary object map
    Vec_Int_t *    vCutFirst;    // first cut of each node
    Vec_Int_t *    vEdgeFirst;   // first edge of each node
    Vec_Int_t *    vDelayFirst;  // first edge of each node
    Vec_Int_t *    vPolars;      // initial 
    Vec_Int_t *    vLits;        // literals 
    // statistics
    abctime        timeStart;
};


/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Sle_Man_t * Sle_ManAlloc( Gia_Man_t * pGia, int nLevels, int fVerbose )
{
    Sle_Man_t * p   = ABC_CALLOC( Sle_Man_t, 1 );
    p->pGia         = pGia;
    p->nLevels      = nLevels;
    p->fVerbose     = fVerbose;
    p->vMask        = Sle_ManInternalNodeMask( pGia );
    p->vCuts        = Sle_ManComputeCuts( pGia, 4, fVerbose );
    p->vCutFanins   = Sle_ManCollectCutFanins( pGia, p->vCuts, p->vMask );
    p->vFanoutEdges = Vec_WecStart( Gia_ManObjNum(pGia) );
    p->vEdgeCuts    = Vec_WecAlloc( 100 );
    p->vObjMap      = Vec_IntStartFull( Gia_ManObjNum(pGia) );
    p->vCutFirst    = Vec_IntStartFull( Gia_ManObjNum(pGia) );
    p->vEdgeFirst   = Vec_IntStartFull( Gia_ManObjNum(pGia) );
    p->vDelayFirst  = Vec_IntStartFull( Gia_ManObjNum(pGia) );
    p->vPolars      = Vec_IntAlloc( 100 );
    p->vLits        = Vec_IntAlloc( 100 );
    return p;
}
void Sle_ManStop( Sle_Man_t * p )
{
    sat_solver_delete( p->pSat );
    Vec_BitFree( p->vMask );
    Vec_IntFree( p->vCuts );
    Vec_WecFree( p->vCutFanins );
    Vec_WecFree( p->vFanoutEdges );
    Vec_WecFree( p->vEdgeCuts );
    Vec_IntFree( p->vObjMap );
    Vec_IntFree( p->vCutFirst );
    Vec_IntFree( p->vEdgeFirst );
    Vec_IntFree( p->vDelayFirst );
    Vec_IntFree( p->vPolars );
    Vec_IntFree( p->vLits );
    ABC_FREE( p );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sle_ManMarkupVariables( Sle_Man_t * p )
{
    int iObj, Counter = Gia_ManObjNum(p->pGia);
    // node variables
    p->nNodeVars = Counter;
    // cut variables
    Gia_ManForEachAndId( p->pGia, iObj )
    {
        int * pList = Vec_IntEntryP( p->vCuts, Vec_IntEntry(p->vCuts, iObj) );
        Vec_IntWriteEntry( p->vCutFirst, iObj, Counter );
        Counter += pList[0] - 1;
    }
    p->nCutVars = Counter - p->nNodeVars;
    // edge variables
    Gia_ManForEachAndId( p->pGia, iObj )
    {
        Vec_IntWriteEntry( p->vEdgeFirst, iObj, Counter );
        Counter += Vec_IntSize( Vec_WecEntry(p->vCutFanins, iObj) );
    }
    p->nEdgeVars = Counter - p->nCutVars - p->nNodeVars;
    // delay variables
    Gia_ManForEachAndId( p->pGia, iObj )
    {
        Vec_IntWriteEntry( p->vDelayFirst, iObj, Counter );
        Counter += p->nLevels;
    }
    p->nDelayVars = Counter - p->nEdgeVars - p->nCutVars - p->nNodeVars;
    p->nVarsTotal = Counter;
    printf( "Vars:  Total = %d.  Node = %d. Cut = %d. Edge = %d. Delay = %d.\n", 
        p->nVarsTotal, p->nNodeVars, p->nCutVars, p->nEdgeVars, p->nDelayVars );
}


/**Function*************************************************************

  Synopsis    [Derive initial variable assignment.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sle_ManDeriveInit( Sle_Man_t * p )
{
    Vec_Int_t * vEdges;
    int pFaninsCopy[16];
    int i, iObj, iFanin, iEdge;
    if ( !Gia_ManHasMapping(p->pGia) )
        return;
    // derive initial state
    Vec_IntClear( p->vPolars );
    Gia_ManForEachAndId( p->pGia, iObj )
    {
        int nFanins, * pFanins, * pCut, * pList, iFound = -1;
        if ( !Gia_ObjIsLut(p->pGia, iObj) )
            continue;
        Vec_IntPush( p->vPolars, iObj ); // node var
        nFanins = Gia_ObjLutSize( p->pGia, iObj );
        pFanins = Gia_ObjLutFanins( p->pGia, iObj );
        // duplicate and sort fanins
        memcpy( pFaninsCopy, pFanins, sizeof(int)*nFanins );
        Vec_IntSelectSort( pFaninsCopy, nFanins );
        // find cut
        pList = Vec_IntEntryP( p->vCuts, Vec_IntEntry(p->vCuts, iObj) );
        Sle_ForEachCut( pList, pCut, i )
            if ( i < pList[0]-1 && nFanins == Sle_CutSize(pCut) && !memcmp(pFaninsCopy, Sle_CutLeaves(pCut), sizeof(int)*Sle_CutSize(pCut)) )
            {
                iFound = i;
                break;
            }
        if ( iFound == -1 )
        {
            printf( "Cannot find the following cut at node %d: {", iObj );
            for ( i = 0; i < nFanins; i++ )
                printf( " %d", pFaninsCopy[i] );
            printf( " }\n" );
            Sle_ManPrintCuts( p->pGia, p->vCuts, iObj );
            fflush( stdout );
        }
        assert( iFound >= 0 );
        Vec_IntPush( p->vPolars, Vec_IntEntry(p->vCutFirst, iObj) + iFound ); // cut var
        // check if the cut contains only primary inputs
        iFound = 0;
        for ( i = 0; i < nFanins; i++ )
            if ( Vec_BitEntry(p->vMask, pFanins[i]) ) // internal node
                iFound = 1;
        if ( !iFound ) // did not find
            Vec_IntPush( p->vPolars, Vec_IntEntry(p->vDelayFirst, iObj) ); // delay var
    }
    // find zero-delay edges
    if ( !p->pGia->vEdge1 )
        return;
    vEdges = Gia_ManEdgeToArray( p->pGia );
    Vec_IntForEachEntryDouble( vEdges, iFanin, iObj, i )
    {
        assert( iFanin < iObj );
        assert( Gia_ObjIsLut(p->pGia, iFanin) );
        assert( Gia_ObjIsLut(p->pGia, iObj) );
        assert( Gia_ObjIsAnd(Gia_ManObj(p->pGia, iFanin)) );
        assert( Gia_ObjIsAnd(Gia_ManObj(p->pGia, iObj)) );
        // find edge
        iEdge = Vec_IntFind( Vec_WecEntry(p->vCutFanins, iObj), iFanin );
        assert( iEdge >= 0 );
        Vec_IntPush( p->vPolars, Vec_IntEntry(p->vEdgeFirst, iObj) + iEdge ); // edge
    }
    Vec_IntFree( vEdges );
}

/**Function*************************************************************

  Synopsis    [Derive CNF.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sle_ManDeriveCnf( Sle_Man_t * p )
{
    int nBTLimit = 0;
    int nTimeOut = 0;
    int i, iObj, value;
    Vec_Int_t * vArray;

    // start the SAT solver
    p->pSat = sat_solver_new();
    sat_solver_setnvars( p->pSat, p->nVarsTotal );
    sat_solver_set_resource_limits( p->pSat, nBTLimit, 0, 0, 0 );
    sat_solver_set_runtime_limit( p->pSat, nTimeOut ? nTimeOut * CLOCKS_PER_SEC + Abc_Clock(): 0 );
    sat_solver_set_random( p->pSat, 1 );
    sat_solver_set_polarity( p->pSat, Vec_IntArray(p->vPolars), Vec_IntSize(p->vPolars) );
    sat_solver_set_var_activity( p->pSat, NULL, p->nVarsTotal );

    // set drivers to be mapped
    Gia_ManForEachCoDriverId( p->pGia, iObj, i )
    {
        Vec_IntFill( p->vLits, 1, Abc_Var2Lit(iObj, 0) ); // pos lit
        value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
        assert( value );
    }

    // add cover clauses and edge-to-cut clauses
    Gia_ManForEachAndId( p->pGia, iObj )
    {
        int e, iEdge, nEdges = 0, Entry;
        int iCutVar0  = Vec_IntEntry( p->vCutFirst, iObj );
        int iEdgeVar0 = Vec_IntEntry( p->vEdgeFirst, iObj );
        int * pCut, * pList  = Vec_IntEntryP( p->vCuts, Vec_IntEntry(p->vCuts, iObj) );
        Vec_Int_t * vCutFans = Vec_WecEntry( p->vCutFanins, iObj );
        assert( iCutVar0 > 0 && iEdgeVar0 > 0 );
        // node requires one of the cuts
        Vec_IntFill( p->vLits, 1, Abc_Var2Lit(iObj, 1) ); // neg lit
        for ( i = 0; i < pList[0]-1; i++ )
            Vec_IntPush( p->vLits, Abc_Var2Lit(iCutVar0 + i, 0) );
        value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
        assert( value );
        // cut required fanin nodes
        Vec_WecInit( p->vEdgeCuts, Vec_IntSize(vCutFans) );
        Sle_ForEachCut( pList, pCut, i )
        {
            int nSize   = Sle_CutSize(pCut);
            int k, * pC = Sle_CutLeaves(pCut);
            if ( nSize < 2 )
                continue;
            for ( k = 0; k < nSize; k++ )
            {
                if ( !Vec_BitEntry(p->vMask, pC[k]) )
                    continue;
                Vec_IntFillTwo( p->vLits, 2, Abc_Var2Lit(iCutVar0 + i, 1), Abc_Var2Lit(pC[k], 0) );
                value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
                assert( value );
                // find the edge ID between pC[k] and iObj
                iEdge = Vec_IntEntry(p->vObjMap, pC[k]);
                if ( iEdge == -1 )
                    Vec_IntWriteEntry( p->vObjMap, pC[k], (iEdge = nEdges++) );
                Vec_WecPush( p->vEdgeCuts, iEdge, iCutVar0 + i );
                Vec_WecPush( p->vFanoutEdges, pC[k], iEdgeVar0 + iEdge );
                p->nCutClas++;
            }
        }
        assert( nEdges == Vec_IntSize(vCutFans) );
        // edge requires one of the fanout cuts
        Vec_WecForEachLevel( p->vEdgeCuts, vArray, e )
        {
            assert( Vec_IntSize(vArray) > 0 );
            Vec_IntFill( p->vLits, 1, Abc_Var2Lit(iEdgeVar0 + e, 1) ); // neg lit (edge)
            Vec_IntForEachEntry( vArray, Entry, i )
                Vec_IntPush( p->vLits, Abc_Var2Lit(Entry, 0) ); // pos lit (cut)
            value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
            assert( value );
            p->nEdgeClas++;
        }
        // clean object map
        Vec_IntForEachEntry( vCutFans, Entry, i )
            Vec_IntWriteEntry( p->vObjMap, Entry, -1 );
    }

    // mutual exclusivity of edges
    Vec_WecForEachLevel( p->vFanoutEdges, vArray, iObj )
    {
        int j, k, EdgeJ, EdgeK;
        int iEdgeVar0 = Vec_IntEntry( p->vEdgeFirst, iObj );
        Vec_Int_t * vCutFans = Vec_WecEntry( p->vCutFanins, iObj );
        // add fanin edges
        for ( i = 0; i < Vec_IntSize(vCutFans); i++ )
            Vec_IntPush( vArray, iEdgeVar0 + i );
        // generate pairs
        Vec_IntForEachEntry( vArray, EdgeJ, j )
            Vec_IntForEachEntryStart( vArray, EdgeK, k, j+1 )
            {
                Vec_IntFillTwo( p->vLits, 2, Abc_Var2Lit(EdgeJ, 1), Abc_Var2Lit(EdgeK, 1) );
                value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
                assert( value );
            }
        p->nEdgeClas2 += Vec_IntSize(vArray) * (Vec_IntSize(vArray) - 1) / 2;
    }

    // add delay clauses
    Gia_ManForEachAndId( p->pGia, iObj )
    {
        int e, iFanin;
        int iEdgeVar0 = Vec_IntEntry( p->vEdgeFirst, iObj );
        int iDelayVar0 = Vec_IntEntry( p->vDelayFirst, iObj );
        Vec_Int_t * vCutFans = Vec_WecEntry( p->vCutFanins, iObj );
        // check if the node has cuts containing only primary inputs
        int * pCut, * pList  = Vec_IntEntryP( p->vCuts, Vec_IntEntry(p->vCuts, iObj) );
        Sle_ForEachCut( pList, pCut, i )
        {
            int nSize   = Sle_CutSize(pCut);
            int k, * pC = Sle_CutLeaves(pCut);
            int fFound  = 0;
            for ( k = 0; k < nSize; k++ )
                if ( Vec_BitEntry(p->vMask, pC[k]) ) // internal node
                    fFound = 1;
            if ( fFound ) // found internal node
                continue;
            Vec_IntFill( p->vLits, 1, Abc_Var2Lit(iDelayVar0, 0) ); // pos lit
            value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
            assert( value );
            //printf( "Setting unit delay for node %d (var %d).\n", iObj, iDelayVar0 );
            break;
        }
        if ( i < pList[0] - 1 )
            continue;
        // create delay requirements for each cut fanin of this node
        Vec_IntForEachEntry( vCutFans, iFanin, e )
        {
            int d, iDelayVarIn = Vec_IntEntry( p->vDelayFirst, iFanin );
            for ( d = 0; d < p->nLevels-1; d++ )
            {
                Vec_IntClear( p->vLits );
                Vec_IntPush( p->vLits, Abc_Var2Lit(iObj, 1) );
                Vec_IntPush( p->vLits, Abc_Var2Lit(iDelayVarIn + d, 1) );
                Vec_IntPush( p->vLits, Abc_Var2Lit(iEdgeVar0 + e, 0) );
                Vec_IntPush( p->vLits, Abc_Var2Lit(iDelayVar0 + d+1, 0) );
                value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
                assert( value );

                Vec_IntClear( p->vLits );
                Vec_IntPush( p->vLits, Abc_Var2Lit(iObj, 1) );
                Vec_IntPush( p->vLits, Abc_Var2Lit(iDelayVarIn + d, 1) );
                Vec_IntPush( p->vLits, Abc_Var2Lit(iEdgeVar0 + e, 1) );
                Vec_IntPush( p->vLits, Abc_Var2Lit(iDelayVar0 + d, 0) );
                value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
                assert( value );
            }

            Vec_IntClear( p->vLits );
            Vec_IntPush( p->vLits, Abc_Var2Lit(iObj, 1) );
            Vec_IntPush( p->vLits, Abc_Var2Lit(iDelayVarIn + d, 1) );
            Vec_IntPush( p->vLits, Abc_Var2Lit(iEdgeVar0 + e, 0) );
            value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
            assert( value );

            Vec_IntClear( p->vLits );
            Vec_IntPush( p->vLits, Abc_Var2Lit(iObj, 1) );
            Vec_IntPush( p->vLits, Abc_Var2Lit(iDelayVarIn + d, 1) );
            Vec_IntPush( p->vLits, Abc_Var2Lit(iDelayVar0 + d, 0) );
            value = sat_solver_addclause( p->pSat, Vec_IntArray(p->vLits), Vec_IntLimit(p->vLits)  );
            assert( value );

            p->nDelayClas += 2*p->nLevels;
        }
    }
    printf( "Clas:  Total = %d.  Cut = %d. Edge = %d. EdgeEx = %d. Delay = %d.\n", 
        sat_solver_nclauses(p->pSat), p->nCutClas, p->nEdgeClas, p->nEdgeClas2, p->nDelayClas );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sle_ManDeriveResult( Sle_Man_t * p, Vec_Int_t * vEdge2, Vec_Int_t * vMapping )
{
    int iObj;
    // create mapping
    Vec_IntFill( vMapping, Gia_ManObjNum(p->pGia), 0 );
    Gia_ManForEachAndId( p->pGia, iObj )
    {
        int i, iCut, iCutVar0 = Vec_IntEntry( p->vCutFirst, iObj );
        int * pCut, * pList = Vec_IntEntryP( p->vCuts, Vec_IntEntry(p->vCuts, iObj) );
        if ( !sat_solver_var_value(p->pSat, iObj) )
            continue;
        Sle_ForEachCut( pList, pCut, iCut )
            if ( sat_solver_var_value(p->pSat, iCutVar0 + iCut) )
                break;
        assert( iCut < pList[0] - 1 );
        Vec_IntWriteEntry( vMapping, iObj, Vec_IntSize(vMapping) );
        Vec_IntPush( vMapping, Sle_CutSize(pCut) );
        for ( i = 0; i < Sle_CutSize(pCut); i++ )
            Vec_IntPush( vMapping, Sle_CutLeaves(pCut)[i] );
        Vec_IntPush( vMapping, iObj );
    }
    // collect edges
    Vec_IntClear( vEdge2 );
    Gia_ManForEachAndId( p->pGia, iObj )
    {
        int i, iFanin, iEdgeVar0 = Vec_IntEntry( p->vEdgeFirst, iObj );
        Vec_Int_t * vCutFans = Vec_WecEntry( p->vCutFanins, iObj );
        if ( !sat_solver_var_value(p->pSat, iObj) )
            continue;
        Vec_IntForEachEntry( vCutFans, iFanin, i )
            if ( sat_solver_var_value(p->pSat, iEdgeVar0 + i) )
                Vec_IntPushTwo( vEdge2, iFanin, iObj );
    }
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sle_ManExplore( Gia_Man_t * pGia, int DelayInit, int fVerbose )
{
    int fVeryVerbose = 0;
    abctime clk = Abc_Clock();
    Vec_Int_t * vEdges2  = Vec_IntAlloc(1000);
    Vec_Int_t * vMapping = Vec_IntAlloc(1000);
    int i, iLut, nConfs, status, Delay, iFirstVar;
    int DelayStart = DelayInit ? DelayInit : Gia_ManLutLevel(pGia, NULL);
    Sle_Man_t * p = Sle_ManAlloc( pGia, DelayStart, fVerbose );
    Sle_ManMarkupVariables( p );
    Sle_ManDeriveInit( p );
    Sle_ManDeriveCnf( p );
    for ( Delay = DelayStart; Delay >= 0; Delay-- )
    {
        // we constrain COs, although it would be fine to constrain only POs
        if ( Delay < DelayStart )
        {
            Gia_ManForEachCoDriverId( p->pGia, iLut, i )
            {
                iFirstVar = Vec_IntEntry( p->vDelayFirst, iLut );
                if ( !sat_solver_push(p->pSat, Abc_Var2Lit(iFirstVar + Delay, 1)) )
                    break;
            }
            if ( i < Gia_ManCoNum(p->pGia) )
            {
                printf( "Proved UNSAT for delay %d.  ", Delay );
                Abc_PrintTime( 1, "Time", Abc_Clock() - clk );
                break;
            }
        }
        // solve with assumptions
        //clk = Abc_Clock();
        nConfs = sat_solver_nconflicts( p->pSat );
        status = sat_solver_solve_internal( p->pSat );
        nConfs = sat_solver_nconflicts( p->pSat ) - nConfs;
        if ( status == l_True )
        {
            if ( fVerbose )
            {
                int nNodes = 0, nEdges = 0;
                for ( i = 0; i < p->nNodeVars; i++ )
                    nNodes += sat_solver_var_value(p->pSat, i);
                for ( i = 0; i < p->nEdgeVars; i++ )
                    nEdges += sat_solver_var_value(p->pSat, p->nNodeVars + p->nCutVars + i);
                printf( "Solution with delay %2d, node count %5d, and edge count %5d exists. Conf = %8d.  ", Delay, nNodes, nEdges, nConfs );
                Abc_PrintTime( 1, "Time", Abc_Clock() - clk );
            }
            // save the result
            Sle_ManDeriveResult( p, vEdges2, vMapping );
            if ( fVeryVerbose )
            {
                printf( "Nodes:  " );
                for ( i = 0; i < p->nNodeVars; i++ )
                    if ( sat_solver_var_value(p->pSat, i) )
                        printf( "%d ", i );
                printf( "\n" );
                printf( "\n" );

                Vec_IntPrint( p->vCutFirst );
                printf( "Cuts:   " );
                for ( i = 0; i < p->nCutVars; i++ )
                    if ( sat_solver_var_value(p->pSat, p->nNodeVars + i) )
                        printf( "%d ", p->nNodeVars + i );
                printf( "\n" );
                printf( "\n" );

                Vec_IntPrint( p->vEdgeFirst );
                printf( "Edges:  " );
                for ( i = 0; i < p->nEdgeVars; i++ )
                    if ( sat_solver_var_value(p->pSat, p->nNodeVars + p->nCutVars + i) )
                        printf( "%d ", p->nNodeVars + p->nCutVars + i );
                printf( "\n" );
                printf( "\n" );

                Vec_IntPrint( p->vDelayFirst );
                printf( "Delays: " );
                for ( i = 0; i < p->nDelayVars; i++ )
                    if ( sat_solver_var_value(p->pSat, p->nNodeVars + p->nCutVars + p->nEdgeVars + i) )
                        printf( "%d ", p->nNodeVars + p->nCutVars + p->nEdgeVars + i );
                printf( "\n" );
                printf( "\n" );
            }
        }
        else
        {
            if ( fVerbose )
            {
                if ( status == l_False )
                    printf( "Proved UNSAT for delay %d.  ", Delay );
                else
                    printf( "Resource limit reached for delay %d.  ", Delay );
                Abc_PrintTime( 1, "Time", Abc_Clock() - clk );
            }
            break;
        }
    }
    if ( Vec_IntSize(vMapping) > 0 )
    {
        Gia_ManEdgeFromArray( p->pGia, vEdges2 );
        Vec_IntFree( vEdges2 );
        Vec_IntFreeP( &p->pGia->vMapping );
        p->pGia->vMapping = vMapping;
    }
    else
    {
        Vec_IntFree( vEdges2 );
        Vec_IntFree( vMapping );
    }
    Sle_ManStop( p );
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

