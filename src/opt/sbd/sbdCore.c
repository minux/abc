/**CFile****************************************************************

  FileName    [sbd.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [SAT-based optimization using internal don't-cares.]

  Synopsis    []

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: sbd.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "sbdInt.h"
#include "opt/dau/dau.h"

ABC_NAMESPACE_IMPL_START

////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

#define SBD_MAX_LUTSIZE 6


typedef struct Sbd_Man_t_ Sbd_Man_t;
struct Sbd_Man_t_
{
    Sbd_Par_t *     pPars;       // user's parameters
    Gia_Man_t *     pGia;        // user's AIG manager (will be modified by adding nodes)
    Vec_Wec_t *     vTfos;       // TFO for each node (roots are marked) (windowing)
    Vec_Int_t *     vLutLevs;    // LUT level for each node after resynthesis
    Vec_Int_t *     vLutCuts;    // LUT cut for each nodes after resynthesis
    Vec_Int_t *     vMirrors;    // alternative node
    Vec_Wrd_t *     vSims[4];    // simulation information (main, backup, controlability)
    Vec_Int_t *     vCover;      // temporary
    Vec_Int_t *     vLits;       // temporary
    // target node
    int             Pivot;       // target node
    Vec_Int_t *     vTfo;        // TFO (excludes node, includes roots) - precomputed
    Vec_Int_t *     vRoots;      // TFO root nodes
    Vec_Int_t *     vWinObjs;    // TFI + Pivot + sideTFI + TFO (including roots)
    Vec_Int_t *     vObj2Var;    // SAT variables for the window (indexes of objects in vWinObjs)
    Vec_Int_t *     vDivVars;    // divisor variables
    Vec_Int_t *     vDivValues;  // SAT variables values for the divisor variables
    Vec_Wec_t *     vDivLevels;  // divisors collected by levels
    Vec_Int_t *     vCounts[2];  // counters of zeros and ones
    Vec_Wrd_t *     vMatrix;     // covering matrix
    sat_solver *    pSat;        // SAT solver
};

static inline int *  Sbd_ObjCut( Sbd_Man_t * p, int i )  { return Vec_IntEntryP( p->vLutCuts, (p->pPars->nLutSize + 1) * i ); }

static inline word * Sbd_ObjSim0( Sbd_Man_t * p, int i ) { return Vec_WrdEntryP( p->vSims[0], p->pPars->nWords * i );         }
static inline word * Sbd_ObjSim1( Sbd_Man_t * p, int i ) { return Vec_WrdEntryP( p->vSims[1], p->pPars->nWords * i );         }
static inline word * Sbd_ObjSim2( Sbd_Man_t * p, int i ) { return Vec_WrdEntryP( p->vSims[2], p->pPars->nWords * i );         }
static inline word * Sbd_ObjSim3( Sbd_Man_t * p, int i ) { return Vec_WrdEntryP( p->vSims[3], p->pPars->nWords * i );         }

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sbd_ParSetDefault( Sbd_Par_t * pPars )
{
    memset( pPars, 0, sizeof(Sbd_Par_t) );
    pPars->nLutSize     = 4;  // target LUT size
    pPars->nTfoLevels   = 3;  // the number of TFO levels (windowing)
    pPars->nTfoFanMax   = 4;  // the max number of fanouts (windowing)
    pPars->nWinSizeMax  = 0;  // maximum window size (windowing)
    pPars->nBTLimit     = 0;  // maximum number of SAT conflicts 
    pPars->nWords       = 1;  // simulation word count
    pPars->fArea        = 0;  // area-oriented optimization
    pPars->fVerbose     = 0;  // verbose flag
    pPars->fVeryVerbose = 0;  // verbose flag
}

/**Function*************************************************************

  Synopsis    [Computes TFO and window roots for all nodes.]

  Description [TFO does not include the node itself. If TFO is empty,
  it means that the node itself is its own root, which may happen if
  the node is pointed by a PO or if it has too many fanouts.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Wec_t * Sbd_ManWindowRoots( Gia_Man_t * p, int nTfoLevels, int nTfoFanMax )
{
    Vec_Wec_t * vTfos = Vec_WecStart( Gia_ManObjNum(p) ); // TFO nodes with roots marked
    Vec_Wec_t * vTemp = Vec_WecStart( Gia_ManObjNum(p) ); // storage
    Vec_Int_t * vNodes, * vNodes0, * vNodes1;
    Vec_Bit_t * vPoDrivers = Vec_BitStart( Gia_ManObjNum(p) );
    int i, k, k2, Id, Fan;
    Gia_ManLevelNum( p );
    Gia_ManCreateRefs( p );
    Gia_ManCleanMark0( p );
    Gia_ManForEachCiId( p, Id, i )
    {
        vNodes = Vec_WecEntry( vTemp, Id );
        Vec_IntGrow( vNodes, 1 );
        Vec_IntPush( vNodes, Id );
    }
    Gia_ManForEachCoDriverId( p, Id, i )
        Vec_BitWriteEntry( vPoDrivers, Id, 1 );
    Gia_ManForEachAndId( p, Id )
    {
        int fAlwaysRoot = Vec_BitEntry(vPoDrivers, Id) || (Gia_ObjRefNumId(p, Id) >= nTfoFanMax);
        vNodes0 = Vec_WecEntry( vTemp, Gia_ObjFaninId0(Gia_ManObj(p, Id), Id) );
        vNodes1 = Vec_WecEntry( vTemp, Gia_ObjFaninId1(Gia_ManObj(p, Id), Id) );
        vNodes = Vec_WecEntry( vTemp, Id );
        Vec_IntTwoMerge2( vNodes0, vNodes1, vNodes );
        k2 = 0;
        Vec_IntForEachEntry( vNodes, Fan, k )
        {
            int fRoot = fAlwaysRoot || (Gia_ObjLevelId(p, Id) - Gia_ObjLevelId(p, Fan) >= nTfoLevels);
            Vec_WecPush( vTfos, Fan, Abc_Var2Lit(Id, fRoot) );
            if ( !fRoot ) Vec_IntWriteEntry( vNodes, k2++, Fan );
        }
        Vec_IntShrink( vNodes, k2 );
        if ( !fAlwaysRoot )
            Vec_IntPush( vNodes, Id );
    }
    Vec_WecFree( vTemp );
    Vec_BitFree( vPoDrivers );

    // print the results
    if ( 1 )
    Vec_WecForEachLevel( vTfos, vNodes, i )
    {
        if ( !Gia_ObjIsAnd(Gia_ManObj(p, i)) )
            continue;
        printf( "Node %3d : ", i );
        Vec_IntForEachEntry( vNodes, Fan, k )
            printf( "%d%s ", Abc_Lit2Var(Fan), Abc_LitIsCompl(Fan)? "*":"" );
        printf( "\n" );
    }

    return vTfos;
}

/**Function*************************************************************

  Synopsis    [Manager manipulation.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Sbd_Man_t * Sbd_ManStart( Gia_Man_t * pGia, Sbd_Par_t * pPars )
{
    int i, w, Id;
    Sbd_Man_t * p = ABC_CALLOC( Sbd_Man_t, 1 );  
    p->pPars      = pPars;
    p->pGia       = pGia;
    p->vTfos      = Sbd_ManWindowRoots( pGia, pPars->nTfoLevels, pPars->nTfoFanMax );
    p->vLutLevs   = Vec_IntStart( Gia_ManObjNum(pGia) );
    p->vLutCuts   = Vec_IntStart( Gia_ManObjNum(pGia) * (p->pPars->nLutSize + 1) );
    p->vMirrors   = Vec_IntStartFull( Gia_ManObjNum(pGia) );
    for ( i = 0; i < 4; i++ )
        p->vSims[i] = Vec_WrdStart( Gia_ManObjNum(pGia) * p->pPars->nWords );
    // target node
    p->vCover     = Vec_IntAlloc( 100 );
    p->vLits      = Vec_IntAlloc( 100 );
    p->vRoots     = Vec_IntAlloc( 100 );
    p->vWinObjs   = Vec_IntAlloc( Gia_ManObjNum(pGia) );
    p->vObj2Var   = Vec_IntStart( Gia_ManObjNum(pGia) );
    p->vDivVars   = Vec_IntAlloc( 100 );
    p->vDivValues = Vec_IntAlloc( 100 );
    p->vDivLevels = Vec_WecAlloc( 100 );
    p->vCounts[0] = Vec_IntAlloc( 100 );
    p->vCounts[1] = Vec_IntAlloc( 100 );
    p->vMatrix    = Vec_WrdAlloc( 100 );
    // start input cuts
    Gia_ManForEachCiId( pGia, Id, i )
    {
        int * pCut = Sbd_ObjCut( p, Id );
        pCut[0] = 1;
        pCut[1] = Id;
    }
    // generate random input
    Gia_ManRandom( 1 );
    Gia_ManForEachCiId( pGia, Id, i )
        for ( w = 0; w < p->pPars->nWords; w++ )
            Sbd_ObjSim0(p, Id)[w] = Gia_ManRandomW( 0 );
    return p;
}
void Sbd_ManStop( Sbd_Man_t * p )
{
    int i;
    Vec_WecFree( p->vTfos );
    Vec_IntFree( p->vLutLevs );
    Vec_IntFree( p->vLutCuts );
    Vec_IntFree( p->vMirrors );
    for ( i = 0; i < 4; i++ )
        Vec_WrdFree( p->vSims[i] );
    Vec_IntFree( p->vCover );
    Vec_IntFree( p->vLits );
    Vec_IntFree( p->vRoots );
    Vec_IntFree( p->vWinObjs );
    Vec_IntFree( p->vObj2Var );
    Vec_IntFree( p->vDivVars );
    Vec_IntFree( p->vDivValues );
    Vec_WecFree( p->vDivLevels );
    Vec_IntFree( p->vCounts[0] );
    Vec_IntFree( p->vCounts[1] );
    Vec_WrdFree( p->vMatrix );
    if ( p->pSat ) sat_solver_delete( p->pSat );
    ABC_FREE( p );
}


/**Function*************************************************************

  Synopsis    [Constructing window.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sbd_ManWindowSim_rec( Sbd_Man_t * p, int Node )
{
    Gia_Obj_t * pObj;
    if ( Vec_IntEntry(p->vMirrors, Node) >= 0 )
        Node = Abc_Lit2Var( Vec_IntEntry(p->vMirrors, Node) );
    if ( Gia_ObjIsTravIdCurrentId(p->pGia, Node) || Node == 0 )
        return;
    Gia_ObjSetTravIdCurrentId(p->pGia, Node);
    pObj = Gia_ManObj( p->pGia, Node );
    if ( Gia_ObjIsAnd(pObj) )
    {
        Sbd_ManWindowSim_rec( p, Gia_ObjFaninId0(pObj, Node) );
        Sbd_ManWindowSim_rec( p, Gia_ObjFaninId1(pObj, Node) );
    }
    if ( !pObj->fMark0 )
    {
        Vec_IntWriteEntry( p->vObj2Var, Node, Vec_IntSize(p->vWinObjs) );
        Vec_IntPush( p->vWinObjs, Node );
    }
    if ( Gia_ObjIsCi(pObj) )
        return;
    // simulate
    assert( Gia_ObjIsAnd(pObj) );
    if ( Gia_ObjIsXor(pObj) )
    {
        Abc_TtXor( Sbd_ObjSim0(p, Node), 
            Sbd_ObjSim0(p, Gia_ObjFaninId0(pObj, Node)), 
            Sbd_ObjSim0(p, Gia_ObjFaninId1(pObj, Node)), 
            p->pPars->nWords, 
            Gia_ObjFaninC0(pObj) ^ Gia_ObjFaninC1(pObj) );

        if ( pObj->fMark0 )
            Abc_TtXor( Sbd_ObjSim1(p, Node), 
                Gia_ObjFanin0(pObj)->fMark0 ? Sbd_ObjSim1(p, Gia_ObjFaninId0(pObj, Node)) : Sbd_ObjSim0(p, Gia_ObjFaninId0(pObj, Node)), 
                Gia_ObjFanin1(pObj)->fMark0 ? Sbd_ObjSim1(p, Gia_ObjFaninId1(pObj, Node)) : Sbd_ObjSim0(p, Gia_ObjFaninId1(pObj, Node)), 
                p->pPars->nWords, 
                Gia_ObjFaninC0(pObj) ^ Gia_ObjFaninC1(pObj) );
    }
    else
    {
        Abc_TtAndCompl( Sbd_ObjSim0(p, Node), 
            Sbd_ObjSim0(p, Gia_ObjFaninId0(pObj, Node)), Gia_ObjFaninC0(pObj), 
            Sbd_ObjSim0(p, Gia_ObjFaninId1(pObj, Node)), Gia_ObjFaninC1(pObj), 
            p->pPars->nWords );

        if ( pObj->fMark0 )
            Abc_TtAndCompl( Sbd_ObjSim1(p, Node), 
                Gia_ObjFanin0(pObj)->fMark0 ? Sbd_ObjSim1(p, Gia_ObjFaninId0(pObj, Node)) : Sbd_ObjSim0(p, Gia_ObjFaninId0(pObj, Node)), Gia_ObjFaninC0(pObj), 
                Gia_ObjFanin1(pObj)->fMark0 ? Sbd_ObjSim1(p, Gia_ObjFaninId1(pObj, Node)) : Sbd_ObjSim0(p, Gia_ObjFaninId1(pObj, Node)), Gia_ObjFaninC1(pObj), 
                p->pPars->nWords );
    }
}
void Sbd_ManPropagateControl( Sbd_Man_t * p, int Node )
{
    Gia_Obj_t * pNode = Gia_ManObj(p->pGia, Node);
    int iObj0 = Gia_ObjFaninId0(pNode, Node);
    int iObj1 = Gia_ObjFaninId1(pNode, Node);

    word * pSims  = Sbd_ObjSim0(p, Node);
    word * pSims0 = Sbd_ObjSim0(p, iObj0);
    word * pSims1 = Sbd_ObjSim0(p, iObj1);

    word * pCtrl  = Sbd_ObjSim2(p, Node);
    word * pCtrl0 = Sbd_ObjSim2(p, iObj0);
    word * pCtrl1 = Sbd_ObjSim2(p, iObj1);

    word * pDtrl  = Sbd_ObjSim3(p, Node);
    word * pDtrl0 = Sbd_ObjSim3(p, iObj0);
    word * pDtrl1 = Sbd_ObjSim3(p, iObj1);

//    printf( "Node %2d : %d %d\n", Node, (int)(pSims[0] & 1), (int)(pCtrl[0] & 1) );
    int w;
    for ( w = 0; w < p->pPars->nWords; w++ )
    {
        word Sim0 = Gia_ObjFaninC0(pNode) ? ~pSims0[w] : pSims0[w];
        word Sim1 = Gia_ObjFaninC1(pNode) ? ~pSims1[w] : pSims1[w];

        pCtrl0[w] = pCtrl[w] & (pSims[w] | Sim1 | (~Sim0 & ~Sim1));
        pCtrl1[w] = pCtrl[w] & (pSims[w] | Sim0);

        pDtrl0[w] = pDtrl[w] & (pSims[w] | Sim1);
        pDtrl1[w] = pDtrl[w] & (pSims[w] | Sim0 | (~Sim0 & ~Sim1));
    }
}
void Sbd_ManUpdateOrder( Sbd_Man_t * p, int Pivot )
{
    int i, k, Node;
    Vec_Int_t * vLevel;
    // collect divisors by logic level
    int LevelMax = Vec_IntEntry(p->vLutLevs, Pivot);
    Vec_WecClear( p->vDivLevels );
    Vec_WecInit( p->vDivLevels, LevelMax + 1 );
    Vec_IntForEachEntry( p->vWinObjs, Node, i )
        Vec_WecPush( p->vDivLevels, Vec_IntEntry(p->vLutLevs, Node), Node );
    // sort primary inputs
    Vec_IntSort( Vec_WecEntry(p->vDivLevels, 0), 0 );
    // reload divisors
    Vec_IntClear( p->vWinObjs );
    Vec_WecForEachLevel( p->vDivLevels, vLevel, i )
    {
        Vec_IntForEachEntry( vLevel, Node, k )
        {
            Vec_IntWriteEntry( p->vObj2Var, Node, Vec_IntSize(p->vWinObjs) );
            Vec_IntPush( p->vWinObjs, Node );
        }
        // detect useful divisors
        if ( i == LevelMax - 2 )
            Vec_IntFill( p->vDivValues, Vec_IntSize(p->vWinObjs), 0 );
    }
}
void Sbd_ManWindow( Sbd_Man_t * p, int Pivot )
{
    int i, Node;
    // assign pivot and TFO (assume siminfo is assigned at the PIs)
    p->Pivot = Pivot;
    p->vTfo = Vec_WecEntry( p->vTfos, Pivot );
    // simulate TFI cone
    Vec_IntClear( p->vWinObjs );
    Gia_ManIncrementTravId( p->pGia );
    Sbd_ManWindowSim_rec( p, Pivot );
    Sbd_ManUpdateOrder( p, Pivot );
    // simulate node
    Gia_ManObj(p->pGia, Pivot)->fMark0 = 1;
    Abc_TtCopy( Sbd_ObjSim1(p, Pivot), Sbd_ObjSim0(p, Pivot), p->pPars->nWords, 1 );
    // mark TFO and simulate extended TFI without adding TFO nodes
    Vec_IntClear( p->vRoots );
    Vec_IntForEachEntry( p->vTfo, Node, i )
    {
        Gia_ManObj(p->pGia, Abc_Lit2Var(Node))->fMark0 = 1;
        if ( !Abc_LitIsCompl(Node) ) 
            continue;
        Sbd_ManWindowSim_rec( p, Abc_Lit2Var(Node) );
        Vec_IntPush( p->vRoots, Abc_Lit2Var(Node) );
    }
    // add TFO nodes and remove marks
    Gia_ManObj(p->pGia, Pivot)->fMark0 = 0;
    Vec_IntForEachEntry( p->vTfo, Node, i )
    {
        Gia_ManObj(p->pGia, Abc_Lit2Var(Node))->fMark0 = 0;
        Vec_IntWriteEntry( p->vObj2Var, Abc_Lit2Var(Node), Vec_IntSize(p->vWinObjs) );
        Vec_IntPush( p->vWinObjs, Abc_Lit2Var(Node) );
    }
    // compute controlability for node
    if ( Vec_IntSize(p->vTfo) == 0 )
        Abc_TtFill( Sbd_ObjSim2(p, Pivot), p->pPars->nWords );
    else
        Abc_TtClear( Sbd_ObjSim2(p, Pivot), p->pPars->nWords );
    Vec_IntForEachEntry( p->vTfo, Node, i )
        if ( Abc_LitIsCompl(Node) ) // root
            Abc_TtOrXor( Sbd_ObjSim2(p, Pivot), Sbd_ObjSim0(p, Abc_Lit2Var(Node)), Sbd_ObjSim1(p, Abc_Lit2Var(Node)), p->pPars->nWords );
    Abc_TtCopy( Sbd_ObjSim3(p, Pivot), Sbd_ObjSim2(p, Pivot), p->pPars->nWords, 0 );
    // propagate controlability to fanins for the TFI nodes starting from the pivot
    for ( i = Vec_IntEntry(p->vObj2Var, Pivot); i >= 0 && ((Node = Vec_IntEntry(p->vWinObjs, i)), 1); i-- )
        if ( Gia_ObjIsAnd(Gia_ManObj(p->pGia, Node)) )
            Sbd_ManPropagateControl( p, Node );
}

/**Function*************************************************************

  Synopsis    [Profiling divisor candidates.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sbd_ManPrintObj( Sbd_Man_t * p, int Pivot )
{
    int nDivs = Vec_IntEntry(p->vObj2Var, Pivot) + 1;
    int i, k, k0, k1, Id, Bit0, Bit1;

    Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        printf( "%d : ", Id ), Extra_PrintBinary( stdout, (unsigned *)Sbd_ObjSim0(p, Id), 64 ), printf( "\n" );

    assert( p->Pivot == Pivot );
    Vec_IntClear( p->vCounts[0] );
    Vec_IntClear( p->vCounts[1] );

    printf( "Node %d.  Useful divisors = %d.\n", Pivot, Vec_IntSize(p->vDivValues) );
    printf( "Lev : " );
    Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
    {
        if ( i == nDivs-1 )
            printf( " " );
        printf( "%d", Vec_IntEntry(p->vLutLevs, Id) );
    }
    printf( "\n" );
    printf( "\n" );

    if ( nDivs > 99 )
    {
        printf( "    : " );
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            if ( i == nDivs-1 )
                printf( " " );
            printf( "%d", Id / 100 );
        }
        printf( "\n" );
    }
    if ( nDivs > 9 )
    {
        printf( "    : " );
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            if ( i == nDivs-1 )
                printf( " " );
            printf( "%d", (Id % 100) / 10 );
        }
        printf( "\n" );
    }
    if ( nDivs > 0 )
    {
        printf( "    : " );
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            if ( i == nDivs-1 )
                printf( " " );
            printf( "%d", Id % 10 );
        }
        printf( "\n" );
        printf( "\n" );
    }

    // sampling matrix
    for ( k = 0; k < p->pPars->nWords * 64; k++ )
    {
        if ( !Abc_TtGetBit(Sbd_ObjSim2(p, Pivot), k) )
            continue;

        printf( "%3d : ", k );
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            word * pSims = Sbd_ObjSim0( p, Id );
            word * pCtrl = Sbd_ObjSim2( p, Id );
            if ( i == nDivs-1 )
            {
                if ( Abc_TtGetBit(pCtrl, k) )
                    Vec_IntPush( p->vCounts[Abc_TtGetBit(pSims, k)], k );
                printf( " " );
            }
            printf( "%c", Abc_TtGetBit(pCtrl, k) ? '0' + Abc_TtGetBit(pSims, k) : '.' );
        }
        printf( "\n" );

        printf( "%3d : ", k );
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            word * pSims = Sbd_ObjSim0( p, Id );
            word * pCtrl = Sbd_ObjSim3( p, Id );
            if ( i == nDivs-1 )
            {
                if ( Abc_TtGetBit(pCtrl, k) )
                    Vec_IntPush( p->vCounts[Abc_TtGetBit(pSims, k)], k );
                printf( " " );
            }
            printf( "%c", Abc_TtGetBit(pCtrl, k) ? '0' + Abc_TtGetBit(pSims, k) : '.' );
        }
        printf( "\n" );

        printf( "Sims: " );
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            word * pSims = Sbd_ObjSim0( p, Id );
            //word * pCtrl = Sbd_ObjSim2( p, Id );
            if ( i == nDivs-1 )
                printf( " " );
            printf( "%c", '0' + Abc_TtGetBit(pSims, k) );
        }
        printf( "\n" );

        printf( "Ctrl: " );
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            //word * pSims = Sbd_ObjSim0( p, Id );
            word * pCtrl = Sbd_ObjSim2( p, Id );
            if ( i == nDivs-1 )
                printf( " " );
            printf( "%c", '0' + Abc_TtGetBit(pCtrl, k) );
        }
        printf( "\n" );


        printf( "\n" );
    }
    // covering table
    printf( "Exploring %d x %d covering table.\n", Vec_IntSize(p->vCounts[0]), Vec_IntSize(p->vCounts[1]) );
/*
    Vec_IntForEachEntryStop( p->vCounts[0], Bit0, k0, Abc_MinInt(Vec_IntSize(p->vCounts[0]), 8) )
    Vec_IntForEachEntryStop( p->vCounts[1], Bit1, k1, Abc_MinInt(Vec_IntSize(p->vCounts[1]), 8) )
    {
        printf( "%3d %3d : ", Bit0, Bit1 );
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            word * pSims = Sbd_ObjSim0( p, Id );
            word * pCtrl = Sbd_ObjSim2( p, Id );
            if ( i == nDivs-1 )
                printf( " " );
            printf( "%c", (Abc_TtGetBit(pCtrl, Bit0) && Abc_TtGetBit(pCtrl, Bit1) && Abc_TtGetBit(pSims, Bit0) != Abc_TtGetBit(pSims, Bit1)) ? '1' : '.' );
        }
        printf( "\n" );
    }
*/
    Vec_WrdClear( p->vMatrix );
    Vec_IntForEachEntryStop( p->vCounts[0], Bit0, k0, Abc_MinInt(Vec_IntSize(p->vCounts[0]), 64) )
    Vec_IntForEachEntryStop( p->vCounts[1], Bit1, k1, Abc_MinInt(Vec_IntSize(p->vCounts[1]), 64) )
    {
        word Row = 0;
        Vec_IntForEachEntryStop( p->vWinObjs, Id, i, nDivs )
        {
            word * pSims = Sbd_ObjSim0( p, Id );
            word * pCtrl = Sbd_ObjSim2( p, Id );
            if ( Abc_TtGetBit(pCtrl, Bit0) && Abc_TtGetBit(pCtrl, Bit1) && Abc_TtGetBit(pSims, Bit0) != Abc_TtGetBit(pSims, Bit1) )
                Abc_TtXorBit( &Row, i );
        }
        if ( !Vec_WrdPushUnique( p->vMatrix, Row ) )
            Extra_PrintBinary( stdout, (unsigned *)&Row, nDivs ), printf( "\n" );
    }

}
int Sbd_ManExplore( Sbd_Man_t * p, int Pivot, word * pTruth )
{
    extern sat_solver * Sbd_ManSatSolver( sat_solver * pSat, Gia_Man_t * p, int Pivot, Vec_Int_t * vWinObjs, Vec_Int_t * vObj2Var, Vec_Int_t * vTfo, Vec_Int_t * vRoots );
    extern word Sbd_ManSolve( sat_solver * pSat, int PivotVar, int FreeVar, Vec_Int_t * vDivVars, Vec_Int_t * vValues, Vec_Int_t * vTemp );

    int PivotVar = Vec_IntEntry(p->vObj2Var, Pivot);
    int FreeVar = Vec_IntSize(p->vWinObjs) + Vec_IntSize(p->vTfo) + Vec_IntSize(p->vRoots);
    int RetValue = 0;

//    Sbd_ManPrintObj( p, Pivot );
    Vec_IntPrint( p->vObj2Var );

    Vec_IntClear( p->vDivVars );
    Vec_IntPush( p->vDivVars, 0 );
    Vec_IntPush( p->vDivVars, 1 );
    Vec_IntPush( p->vDivVars, 2 );
    Vec_IntPush( p->vDivVars, 4 );

    p->pSat = Sbd_ManSatSolver( p->pSat, p->pGia, Pivot, p->vWinObjs, p->vObj2Var, p->vTfo, p->vRoots );
    *pTruth = Sbd_ManSolve( p->pSat, PivotVar, FreeVar, p->vDivVars, p->vDivValues, p->vLits );
    if ( *pTruth == SBD_SAT_UNDEC )
        printf( "Node %d:  Undecided.\n", Pivot );
    else if ( *pTruth == SBD_SAT_SAT )
    {
        int i;
        printf( "Node %d:  SAT.\n", Pivot );
        for ( i = 0; i < Vec_IntSize(p->vDivValues); i++ )
            printf( "%d", Vec_IntEntry(p->vDivValues, i) & 1 );
        printf( "\n" );
        for ( i = 0; i < Vec_IntSize(p->vDivValues); i++ )
            printf( "%d", Vec_IntEntry(p->vDivValues, i) >> 1 );
        printf( "\n" );
    }
    else
    {
        printf( "Node %d:  UNSAT.\n", Pivot );
        RetValue = 1;
    }

    Extra_PrintBinary( stdout, (unsigned *)pTruth, 1 << Vec_IntSize(p->vDivVars) ), printf( "\n" );

    return RetValue;
}

/**Function*************************************************************

  Synopsis    [Computes delay-oriented k-feasible cut at the node.]

  Description [Return 1 if node's LUT level does not exceed those of the fanins.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Sbd_CutMergeSimple( Sbd_Man_t * p, int * pCut1, int * pCut2, int * pCut )
{
    int * pBeg  = pCut + 1;
    int * pBeg1 = pCut1 + 1;
    int * pBeg2 = pCut2 + 1;
    int * pEnd1 = pCut1 + 1 + pCut1[0];
    int * pEnd2 = pCut2 + 1 + pCut2[0];
    while ( pBeg1 < pEnd1 && pBeg2 < pEnd2 )
    {
        if ( *pBeg1 == *pBeg2 )
            *pBeg++ = *pBeg1++, pBeg2++;
        else if ( *pBeg1 < *pBeg2 )
            *pBeg++ = *pBeg1++;
        else 
            *pBeg++ = *pBeg2++;
    }
    while ( pBeg1 < pEnd1 )
        *pBeg++ = *pBeg1++;
    while ( pBeg2 < pEnd2 )
        *pBeg++ = *pBeg2++;
    return (pCut[0] = pBeg - pCut - 1);
}
int Sbd_ManComputeCut( Sbd_Man_t * p, int Node )
{
    int pCut[2*SBD_MAX_LUTSIZE];     
    int iFan0   = Gia_ObjFaninId0( Gia_ManObj(p->pGia, Node), Node );
    int iFan1   = Gia_ObjFaninId1( Gia_ManObj(p->pGia, Node), Node );
    int Level0  = Vec_IntEntry( p->vLutLevs, iFan0 );
    int Level1  = Vec_IntEntry( p->vLutLevs, iFan1 );
    int LevMax  = (Level0 || Level1) ? Abc_MaxInt(Level0, Level1) : 1;
    int * pCut0 = Sbd_ObjCut( p, iFan0 );
    int * pCut1 = Sbd_ObjCut( p, iFan1 );
    int Cut0[2] = {1, iFan0}, * pCut0Temp = Level0 < LevMax ? Cut0 : pCut0; 
    int Cut1[2] = {1, iFan1}, * pCut1Temp = Level1 < LevMax ? Cut1 : pCut1; 
    int nSize   = Sbd_CutMergeSimple( p, pCut0Temp, pCut1Temp, pCut );
    int Result  = 1; // no need to resynthesize
    assert( iFan0 != iFan1 );
    if ( nSize > p->pPars->nLutSize )
    {
        pCut[0] = 2;
        pCut[1] = iFan0 < iFan1 ? iFan0 : iFan1;
        pCut[2] = iFan0 < iFan1 ? iFan1 : iFan0;
        Result  = LevMax ? 0 : 1;
        LevMax++;
    }
    assert( Vec_IntEntry(p->vLutLevs, Node) == 0 );
    Vec_IntWriteEntry( p->vLutLevs, Node, LevMax );
    memcpy( Sbd_ObjCut(p, Node), pCut, sizeof(int) * (pCut[0] + 1) );
    printf( "Setting node %d with delay %d (result = %d).\n", Node, LevMax, Result );
    return Result;
}
int Sbd_ManImplement( Sbd_Man_t * p, int Pivot, word Truth )
{
    int i, k, w, iLit, Node;
    int iObjLast = Gia_ManObjNum(p->pGia);
    int iCurLev = Vec_IntEntry(p->vLutLevs, Pivot);
    // collect leaf literals
    Vec_IntClear( p->vLits );
    Vec_IntForEachEntry( p->vDivVars, Node, i )
    {
        Node = Vec_IntEntry( p->vWinObjs, Node );
        if ( Vec_IntEntry(p->vMirrors, Node) >= 0 )
            Vec_IntPush( p->vLits, Vec_IntEntry(p->vMirrors, Node) );
        else
            Vec_IntPush( p->vLits, Abc_Var2Lit(Node, 0) );
    }
    // pretend to have MUXes
    assert( p->pGia->pMuxes == NULL );
    if ( p->pGia->nXors )
        p->pGia->pMuxes = (unsigned *)p;
    // derive new function of the node
    iLit = Dsm_ManTruthToGia( p->pGia, &Truth, p->vLits, p->vCover );
    p->pGia->pMuxes = NULL;
    // remember this function
    assert( Vec_IntEntry(p->vMirrors, Pivot) == -1 );
    Vec_IntWriteEntry( p->vMirrors, Pivot, iLit );
    // extend data-structure for new nodes
    assert( Vec_IntSize(p->vLutLevs) == iObjLast );
    for ( i = iObjLast; i < Gia_ManObjNum(p->pGia); i++ )
    {
        Vec_IntPush( p->vLutLevs, 0 );
        Vec_IntFillExtra( p->vLutCuts, Vec_IntSize(p->vLutCuts) + p->pPars->nLutSize + 1, 0 );
        Sbd_ManComputeCut( p, i );
        for ( k = 0; k < 4; k++ )
            for ( w = 0; w < p->pPars->nWords; w++ )
                Vec_WrdPush( p->vSims[k], 0 );
    }
    // make sure delay reduction is achieved
    assert( Vec_IntEntry(p->vLutLevs, Abc_Lit2Var(iLit)) < iCurLev );
    return 0;
}

/**Function*************************************************************

  Synopsis    [Derives new AIG after resynthesis.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sbd_ManDerive_rec( Gia_Man_t * pNew, Gia_Man_t * p, int Node, Vec_Int_t * vMirrors )
{
    Gia_Obj_t * pObj;
    int Obj = Node;
    if ( Node < Vec_IntSize(vMirrors) && Vec_IntEntry(vMirrors, Node) >= 0 )
        Obj = Abc_Lit2Var( Vec_IntEntry(vMirrors, Node) );
    pObj = Gia_ManObj( p, Obj );
    if ( ~pObj->Value )
        return;
    assert( Gia_ObjIsAnd(pObj) );
    Sbd_ManDerive_rec( pNew, p, Gia_ObjFaninId0(pObj, Obj), vMirrors );
    Sbd_ManDerive_rec( pNew, p, Gia_ObjFaninId1(pObj, Obj), vMirrors );
    if ( Gia_ObjIsXor(pObj) )
        pObj->Value = Gia_ManHashXorReal( pNew, Gia_ObjFanin0Copy(pObj), Gia_ObjFanin1Copy(pObj) );
    else
        pObj->Value = Gia_ManHashAnd( pNew, Gia_ObjFanin0Copy(pObj), Gia_ObjFanin1Copy(pObj) );
    // set the original node as well
    if ( Obj != Node )
        Gia_ManObj(p, Node)->Value = Abc_LitNotCond( pObj->Value, Abc_LitIsCompl(Vec_IntEntry(vMirrors, Node)) );
} 
Gia_Man_t * Sbd_ManDerive( Gia_Man_t * p, Vec_Int_t * vMirrors )
{
    Gia_Man_t * pNew;
    Gia_Obj_t * pObj;
    int i;
    Gia_ManFillValue( p );
    pNew = Gia_ManStart( Gia_ManObjNum(p) );
    pNew->pName = Abc_UtilStrsav( p->pName );
    pNew->pSpec = Abc_UtilStrsav( p->pSpec );
    Gia_ManConst0(p)->Value = 0;
    Gia_ManHashAlloc( pNew );
    Gia_ManForEachCi( p, pObj, i )
        pObj->Value = Gia_ManAppendCi(pNew);
    Gia_ManForEachCo( p, pObj, i )
        Sbd_ManDerive_rec( pNew, p, Gia_ObjFaninId0p(p, pObj), vMirrors );
    Gia_ManForEachCo( p, pObj, i )
        pObj->Value = Gia_ManAppendCo( pNew, Gia_ObjFanin0Copy(pObj) );
    Gia_ManHashStop( pNew );
    Gia_ManSetRegNum( pNew, Gia_ManRegNum(p) );
    return pNew;
}


/**Function*************************************************************

  Synopsis    [Performs delay optimization for the given LUT size.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Gia_Man_t * Sbd_NtkPerform( Gia_Man_t * pGia, Sbd_Par_t * pPars )
{
    Gia_Man_t * pNew;  
    Sbd_Man_t * p = Sbd_ManStart( pGia, pPars );
    int Pivot; word Truth = 0;
    assert( pPars->nLutSize <= 6 );
    Gia_ManForEachAndId( pGia, Pivot )
    {
        if ( Sbd_ManComputeCut( p, Pivot ) )
            continue;
        printf( "Looking at node %d\n", Pivot );
        Sbd_ManWindow( p, Pivot );
        if ( Sbd_ManExplore( p, Pivot, &Truth ) )
            Sbd_ManImplement( p, Pivot, Truth );
        break;
    }
    pNew = Sbd_ManDerive( pGia, p->vMirrors );
    Sbd_ManStop( p );
    return pNew;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

