/**CFile****************************************************************

  FileName    [sfmDec.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [SAT-based optimization using internal don't-cares.]

  Synopsis    [SAT-based decomposition.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: sfmDec.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "sfmInt.h"
#include "misc/st/st.h"
#include "map/mio/mio.h"
#include "base/abc/abc.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

#define SFM_FAN_MAX 6

typedef struct Sfm_Dec_t_ Sfm_Dec_t;
struct Sfm_Dec_t_
{
    // parameters
    Sfm_Par_t *       pPars;       // parameters
    // library
    Vec_Int_t         vGateSizes;  // fanin counts
    Vec_Wrd_t         vGateFuncs;  // gate truth tables
    Vec_Wec_t         vGateCnfs;   // gate CNFs
    Vec_Ptr_t         vGateHands;  // gate handles
    int               GateConst0;  // special gates
    int               GateConst1;  // special gates
    int               GateBuffer;  // special gates
    int               GateInvert;  // special gates
    int               GateAnd[4];  // special gates
    int               GateOr[4];   // special gates
    // objects
    int               nDivs;       // the number of divisors
    int               nMffc;       // the number of divisors
    int               iTarget;     // target node
    Vec_Int_t         vObjRoots;   // roots of the window
    Vec_Int_t         vObjGates;   // functionality
    Vec_Wec_t         vObjFanins;  // fanin IDs
    Vec_Int_t         vObjMap;     // object map
    Vec_Int_t         vObjDec;     // decomposition
    // solver
    sat_solver *      pSat;        // reusable solver 
    Vec_Wec_t         vClauses;    // CNF clauses for the node
    Vec_Int_t         vImpls[2];   // onset/offset implications
    Vec_Int_t         vCounts[2];  // onset/offset counters
    Vec_Wrd_t         vSets[2];    // onset/offset patterns
    int               nPats[2];    // CEX count
    word              uMask[2];    // mask count
    // temporary
    Vec_Int_t         vTemp;
    Vec_Int_t         vTemp2;
    // statistics
    abctime           timeWin;
    abctime           timeCnf;
    abctime           timeSat;
};

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Setup parameter structure.]

  Description []

  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sfm_ParSetDefault3( Sfm_Par_t * pPars )
{
    memset( pPars, 0, sizeof(Sfm_Par_t) );
    pPars->nTfoLevMax   = 1000;  // the maximum fanout levels
    pPars->nTfiLevMax   = 1000;  // the maximum fanin levels
    pPars->nFanoutMax   =   30;  // the maximum number of fanoutsp
    pPars->nMffcMax     =    3;  // the maximum MFFC size
    pPars->nWinSizeMax  =  300;  // the maximum window size
    pPars->nGrowthLevel =    0;  // the maximum allowed growth in level
    pPars->nBTLimit     = 5000;  // the maximum number of conflicts in one SAT run
    pPars->fArea        =    0;  // performs optimization for area
    pPars->fVerbose     =    0;  // enable basic stats
    pPars->fVeryVerbose =    0;  // enable detailed stats
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Sfm_Dec_t * Sfm_DecStart( Sfm_Par_t * pPars )
{
    Sfm_Dec_t * p = ABC_CALLOC( Sfm_Dec_t, 1 );
    p->pPars = pPars;
    p->pSat = sat_solver_new();
    return p;
}
void Sfm_DecStop( Sfm_Dec_t * p )
{
    // library
    Vec_IntErase( &p->vGateSizes );
    Vec_WrdErase( &p->vGateFuncs );
    Vec_WecErase( &p->vGateCnfs );
    Vec_PtrErase( &p->vGateHands );
    // objects
    Vec_IntErase( &p->vObjRoots );
    Vec_IntErase( &p->vObjGates );
    Vec_WecErase( &p->vObjFanins );
    Vec_IntErase( &p->vObjMap );
    Vec_IntErase( &p->vObjDec );
    // solver
    sat_solver_delete( p->pSat );
    Vec_WecErase( &p->vClauses );
    Vec_IntErase( &p->vImpls[0] );
    Vec_IntErase( &p->vImpls[1] );
    Vec_IntErase( &p->vCounts[0] );
    Vec_IntErase( &p->vCounts[1] );
    Vec_WrdErase( &p->vSets[0] );
    Vec_WrdErase( &p->vSets[1] );
    // temporary
    Vec_IntErase( &p->vTemp );
    Vec_IntErase( &p->vTemp2 );
    ABC_FREE( p );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Sfm_DecPrepareSolver( Sfm_Dec_t * p )
{
    abctime clk = Abc_Clock();
    Vec_Int_t * vRoots = &p->vObjRoots;
    Vec_Int_t * vFaninVars = &p->vTemp2;
    Vec_Int_t * vLevel, * vClause;
    int i, k, Gate, iObj, RetValue;
    int nTfiSize = p->iTarget + 1; // including node
    int nWinSize = Vec_IntSize(&p->vObjGates);
    int nSatVars = 2 * nWinSize - nTfiSize;
    assert( nWinSize == Vec_IntSize(&p->vObjGates) );
    assert( p->iTarget < nWinSize );
    // create SAT solver
    sat_solver_restart( p->pSat );
    sat_solver_setnvars( p->pSat, nSatVars + Vec_IntSize(vRoots) );
    // add CNF clauses for the TFI
    Vec_IntForEachEntry( &p->vObjGates, Gate, i )
    {
        if ( Gate == -1 )
            continue;
        // generate CNF 
        vLevel = Vec_WecEntry( &p->vObjFanins, i );
        Vec_IntPush( vLevel, i );
        Sfm_TranslateCnf( &p->vClauses, (Vec_Str_t *)Vec_WecEntry(&p->vGateCnfs, Gate), vLevel, -1 );
        Vec_IntPop( vLevel );
        // add clauses
        Vec_WecForEachLevel( &p->vClauses, vClause, k )
        {
            if ( Vec_IntSize(vClause) == 0 )
                break;
            RetValue = sat_solver_addclause( p->pSat, Vec_IntArray(vClause), Vec_IntArray(vClause) + Vec_IntSize(vClause) );
            if ( RetValue == 0 )
                return 0;
        }
    }
    // add CNF clauses for the TFO
    Vec_IntForEachEntryStart( &p->vObjGates, Gate, i, nTfiSize )
    {
        assert( Gate != -1 );
        vLevel = Vec_WecEntry( &p->vObjFanins, i );
        Vec_IntClear( vFaninVars );
        Vec_IntForEachEntry( vLevel, iObj, k )
            Vec_IntPush( vFaninVars, iObj <= p->iTarget ? iObj : iObj + nWinSize - nTfiSize );
        Vec_IntPush( vFaninVars, i + nWinSize - nTfiSize );
        // generate CNF 
        Sfm_TranslateCnf( &p->vClauses, (Vec_Str_t *)Vec_WecEntry(&p->vGateCnfs, Gate), vFaninVars, p->iTarget );
        // add clauses
        Vec_WecForEachLevel( &p->vClauses, vClause, k )
        {
            if ( Vec_IntSize(vClause) == 0 )
                break;
            RetValue = sat_solver_addclause( p->pSat, Vec_IntArray(vClause), Vec_IntArray(vClause) + Vec_IntSize(vClause) );
            if ( RetValue == 0 )
                return 0;
        }
    }
    if ( nTfiSize < nWinSize )
    {
        // create XOR clauses for the roots
        Vec_IntClear( vFaninVars );
        Vec_IntForEachEntry( vRoots, iObj, i )
        {
            Vec_IntPush( vFaninVars, Abc_Var2Lit(nSatVars, 0) );
            sat_solver_add_xor( p->pSat, iObj, iObj + nWinSize - nTfiSize, nSatVars++, 0 );
        }
        // make OR clause for the last nRoots variables
        RetValue = sat_solver_addclause( p->pSat, Vec_IntArray(vFaninVars), Vec_IntLimit(vFaninVars) );
        if ( RetValue == 0 )
            return 0;
        assert( nSatVars == sat_solver_nvars(p->pSat) );
    }
    else assert( Vec_IntSize(vRoots) == 1 );
    // finalize
    RetValue = sat_solver_simplify( p->pSat );
    p->timeCnf += Abc_Clock() - clk;
    return 1;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Sfm_DecFindWeight( Sfm_Dec_t * p, int c, int iLit )
{
    int Value = Vec_IntEntry( &p->vCounts[!c], Abc_Lit2Var(iLit) );
    return Abc_LitIsCompl(iLit) ? Value : p->nPats[!c] - Value;
}
int Sfm_DecPeformDecOne( Sfm_Dec_t * p, int * pfConst )
{
    int fVerbose = p->pPars->fVeryVerbose;
    int nBTLimit = 0;
    abctime clk = Abc_Clock();
    int i, k, c, Entry, status, Lits[3], RetValue;
    int iLitBest = -1, iCBest = -1, WeightBest = -1, Weight;
    *pfConst = -1;
    // check stuck-at-0/1 (on/off-set empty)
    p->nPats[0] = p->nPats[1] = 0;
    p->uMask[0] = p->uMask[1] = 0;
    Vec_IntClear( &p->vImpls[0] );
    Vec_IntClear( &p->vImpls[1] );
    Vec_IntClear( &p->vCounts[0] );
    Vec_IntClear( &p->vCounts[1] );
    Vec_WrdClear( &p->vSets[0] );
    Vec_WrdClear( &p->vSets[1] );
    for ( c = 0; c < 2; c++ )
    {
        Lits[0] = Abc_Var2Lit( p->iTarget, c );
        status = sat_solver_solve( p->pSat, Lits, Lits + 1, nBTLimit, 0, 0, 0 );
        if ( status == l_Undef )
            return -2;
        if ( status == l_False )
        {
            *pfConst = c;
            return -1;
        }
        assert( status == l_True );
        // record this status
        for ( i = 0; i <= p->iTarget; i++ )
        {
            Entry = sat_solver_var_value(p->pSat, i);
            Vec_IntPush( &p->vCounts[c], Entry );
            Vec_WrdPush( &p->vSets[c], (word)Entry );
        }
        p->nPats[c]++;
        p->uMask[c] = 1;
    }
    // proceed checking divisors based on their values
    for ( c = 0; c < 2; c++ )
    {
        Lits[0] = Abc_Var2Lit( p->iTarget, c );
        for ( i = 0; i < p->nDivs; i++ )
        {
            word Column = Vec_WrdEntry(&p->vSets[c], i);
            if ( Column != 0 && Column != p->uMask[c] ) // diff value is possible
                continue;
            Lits[1] = Abc_Var2Lit( i, Column != 0 );
            status = sat_solver_solve( p->pSat, Lits, Lits + 2, nBTLimit, 0, 0, 0 );
            if ( status == l_Undef )
                return 0;
            if ( status == l_False )
            {
                Vec_IntPush( &p->vImpls[c], Abc_LitNot(Lits[1]) );
                continue;
            }
            assert( status == l_True );
            if ( p->nPats[c] == 64 )
                continue;
            // record this status
            for ( k = 0; k <= p->iTarget; k++ )
                if ( sat_solver_var_value(p->pSat, k) )
                {
                    Vec_IntAddToEntry( &p->vCounts[c], k, 1 );
                    *Vec_WrdEntryP(&p->vSets[c], k) |= ((word)1 << p->nPats[c]);
                }
            p->uMask[c] |= ((word)1 << p->nPats[c]++);
        }
    }
    // find the best decomposition
    for ( c = 0; c < 2; c++ )
    {
        Vec_IntForEachEntry( &p->vImpls[c], Entry, i )
        {
            Weight = Sfm_DecFindWeight(p, c, Entry);
            if ( WeightBest < Weight )
            {
                WeightBest = Weight;
                iLitBest = Entry;
                iCBest = c;
            }
        }
    }
    if ( WeightBest == -1 )
        return -2;

    // add clause
    Lits[0] = Abc_Var2Lit( p->iTarget, iCBest );
    Lits[1] = iLitBest;
    RetValue = sat_solver_addclause( p->pSat, Lits, Lits + 2 );
    if ( RetValue == 0 )
        return -1;


    p->timeSat += Abc_Clock() - clk;
    // print the results
    if ( !fVerbose )
        return Abc_Var2Lit( iLitBest, iCBest );

    printf( "\nBest literal (%d; %s%d) with weight %d.\n\n", iCBest, Abc_LitIsCompl(iLitBest)? "!":"", Abc_Lit2Var(iLitBest), WeightBest );

    for ( c = 0; c < 2; c++ )
    {
        Vec_Int_t * vLevel = Vec_WecEntry( &p->vObjFanins, p->iTarget );
        printf( "\n%s-SET of object %d (divs = %d) with gate \"%s\" and fanins: ", 
            c ? "OFF": "ON", p->iTarget, p->nDivs, 
            Mio_GateReadName((Mio_Gate_t *)Vec_PtrEntry(&p->vGateHands, Vec_IntEntry(&p->vObjGates,p->iTarget))) );
        Vec_IntForEachEntry( vLevel, Entry, i )
            printf( "%d ", Entry );
        printf( "\n" );

        printf( "Implications: " );
        Vec_IntForEachEntry( &p->vImpls[c], Entry, i )
            printf( "%s%d(%d) ", Abc_LitIsCompl(Entry)? "!":"", Abc_Lit2Var(Entry), Sfm_DecFindWeight(p, c, Entry) );
        printf( "\n" );
        printf( "     " );
        for ( i = 0; i <= p->iTarget; i++ )
            printf( "%d", i / 10 );
        printf( "\n" );
        printf( "     " );
        for ( i = 0; i <= p->iTarget; i++ )
            printf( "%d", i % 10 );
        printf( "\n" );
        for ( k = 0; k < p->nPats[c]; k++ )
        {
            printf( "%2d : ", k );
            for ( i = 0; i <= p->iTarget; i++ )
                printf( "%d", (int)((Vec_WrdEntry(&p->vSets[c], i) >> k) & 1) );
            printf( "\n" );
        }
        printf( "\n" );
    }
    return Abc_Var2Lit( iLitBest, iCBest );
}
int Sfm_DecPeformDec( Sfm_Dec_t * p, Mio_Library_t * pLib )
{
    Vec_Int_t * vLevel;
    int i, Dec, Last, fCompl, Pol, fConst = -1, nNodes = Vec_IntSize(&p->vObjGates);
    // perform decomposition
    Vec_IntClear( &p->vObjDec );
    for ( i = 0; i <= p->nMffc; i++ )
    {
        Dec = Sfm_DecPeformDecOne( p, &fConst );
        if ( Dec == -2 )
        {
            if ( p->pPars->fVerbose )
                printf( "There is no decomposition (or time out occurred).\n" );
            return -1;
        }
        if ( Dec == -1 )
            break;
        Vec_IntPush( &p->vObjDec, Dec );
    }
    if ( i == p->nMffc + 1 )
    {
        if ( p->pPars->fVerbose )
        printf( "Area-reducing decomposition is not found.\n" );
        return -1;
    }
    // check constant
    if ( Vec_IntSize(&p->vObjDec) == 0 )
    {
        assert( fConst >= 0 );
        // report
        if ( p->pPars->fVerbose )
        printf( "Create constant %d.\n", fConst );
        // add gate
        Vec_IntPush( &p->vObjGates, fConst ? p->GateConst1 : p->GateConst0 );
        vLevel = Vec_WecPushLevel( &p->vObjFanins );
        return Vec_IntSize(&p->vObjDec);
    }
    // create network
    Last = Vec_IntPop( &p->vObjDec );
    fCompl = Abc_LitIsCompl(Last);
    Last = Abc_LitNotCond( Abc_Lit2Var(Last), fCompl );
    if ( Vec_IntSize(&p->vObjDec) == 0 )
    {
        // report
        if ( p->pPars->fVerbose )
//        printf( "Create node %d = %s%d.\n", nNodes, (Abc_LitIsCompl(Last) ^ fCompl) ? "!":"", Abc_Lit2Var(Last) );
        printf( "Create node %d = %s%d.\n", nNodes, Abc_LitIsCompl(Last) ? "!":"", Abc_Lit2Var(Last) );
        // add gate
//        Vec_IntPush( &p->vObjGates, (Abc_LitIsCompl(Last) ^ fCompl) ? p->GateInvert : p->GateBuffer );
        Vec_IntPush( &p->vObjGates, Abc_LitIsCompl(Last) ? p->GateInvert : p->GateBuffer );
        vLevel = Vec_WecPushLevel( &p->vObjFanins );
        Vec_IntPush( vLevel, Abc_Lit2Var(Last) );
        return Vec_IntSize(&p->vObjDec);
    }
    Vec_IntForEachEntryReverse( &p->vObjDec, Dec, i )
    {
        fCompl = Abc_LitIsCompl(Dec);
//        Dec = Abc_Lit2Var(Dec);
        Dec = Abc_LitNotCond( Abc_Lit2Var(Dec), fCompl );
        // add gate
        Pol = (Abc_LitIsCompl(Last) << 1) | Abc_LitIsCompl(Dec);
//        Pol = Abc_LitIsCompl(Dec);
        if ( fCompl )
            Vec_IntPush( &p->vObjGates, p->GateOr[Pol] );
        else
            Vec_IntPush( &p->vObjGates, p->GateAnd[Pol] );
        vLevel = Vec_WecPushLevel( &p->vObjFanins );
        Vec_IntPush( vLevel, Abc_Lit2Var(Dec) );
        Vec_IntPush( vLevel, Abc_Lit2Var(Last) );
        // report
        if ( p->pPars->fVerbose )
        printf( "Create node %s%d = %s%d and %s%d (gate %d).\n", 
            fCompl? "!":"", nNodes,
            Abc_LitIsCompl(Last)? "!":"", Abc_Lit2Var(Last),  
            Abc_LitIsCompl(Dec)? "!":"", Abc_Lit2Var(Dec), 
            Pol  );
        // prepare for the next one
        Last = Abc_Var2Lit( nNodes, 0 );
        nNodes++;
    }
    return Vec_IntSize(&p->vObjDec);
}

/**Function*************************************************************

  Synopsis    [Incremental level update.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkUpdateIncLevel_rec( Abc_Obj_t * pObj )
{
    Abc_Obj_t * pFanout;
    int i, LevelNew = Abc_ObjLevelNew(pObj);
    if ( LevelNew == (int)pObj->Level )
        return;
    pObj->Level = LevelNew;
    if ( Abc_ObjIsCo(pObj) )
        return;
    Abc_ObjForEachFanout( pObj, pFanout, i )
        Abc_NtkUpdateIncLevel_rec( pFanout );
}
void Abc_NtkUpdateIncLevel( Abc_Obj_t * pObj )
{
    Abc_NtkUpdateIncLevel_rec( pObj );
}

/**Function*************************************************************

  Synopsis    [Testbench for the new AIG minimization.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
#define SFM_MASK_PI     1  // supp(node) is contained in supp(TFI(pivot))
#define SFM_MASK_INPUT  2  // supp(node) does not overlap with supp(TFI(pivot))
#define SFM_MASK_FANIN  4  // the same as above (pointed to by node with SFM_MASK_PI | SFM_MASK_INPUT)
#define SFM_MASK_MFFC   8  // MFFC nodes, including the target node

void Abc_NtkDfsReverseOne_rec( Abc_Obj_t * pObj, Vec_Int_t * vTfo, int nLevelMax, int nFanoutMax )
{
    Abc_Obj_t * pFanout; int i;
    if ( Abc_NodeIsTravIdCurrent( pObj ) )
        return;
    Abc_NodeSetTravIdCurrent( pObj );
    if ( Abc_ObjIsCo(pObj) || Abc_ObjLevel(pObj) > nLevelMax )
        return;
    assert( Abc_ObjIsNode( pObj ) );
    if ( Abc_ObjFanoutNum(pObj) <= nFanoutMax )
    {
        Abc_ObjForEachFanout( pObj, pFanout, i )
            if ( Abc_ObjIsCo(pFanout) )
                break;
        if ( i == Abc_ObjFanoutNum(pObj) )
            Abc_ObjForEachFanout( pObj, pFanout, i )
                Abc_NtkDfsReverseOne_rec( pFanout, vTfo, nLevelMax, nFanoutMax );
    }
    Vec_IntPush( vTfo, Abc_ObjId(pObj) );
    pObj->iTemp = 0;
}
int Abc_NtkDfsOne_rec( Abc_Obj_t * pObj, Vec_Int_t * vTfi, int nLevelMin, int CiLabel )
{
    Abc_Obj_t * pFanin; int i, Mask = 0;
    if ( Abc_NodeIsTravIdCurrent( pObj ) )
        return pObj->iTemp;
    Abc_NodeSetTravIdCurrent( pObj );
    if ( Abc_ObjIsCi(pObj) || Abc_ObjLevel(pObj) < nLevelMin )
    {
        Vec_IntPush( vTfi, Abc_ObjId(pObj) );
        return (pObj->iTemp = CiLabel);
    }
    assert( Abc_ObjIsNode(pObj) );
    Abc_ObjForEachFanin( pObj, pFanin, i )
        Mask |= Abc_NtkDfsOne_rec( pFanin, vTfi, nLevelMin, CiLabel );
    Vec_IntPush( vTfi, Abc_ObjId(pObj) );
    //assert( Mask > 0 );
    return (pObj->iTemp = Mask);
}
void Sfm_DecAddNode( Abc_Obj_t * pObj, Vec_Int_t * vMap, Vec_Int_t * vGates, int fSkip, int fVeryVerbose )
{
    if ( fVeryVerbose )
        printf( "%d:%d(%d) ", Vec_IntSize(vMap), Abc_ObjId(pObj), pObj->iTemp );
    if ( fVeryVerbose )
        Abc_ObjPrint( stdout, pObj );
    Vec_IntPush( vMap, Abc_ObjId(pObj) );
    Vec_IntPush( vGates, fSkip ? -1 : Mio_GateReadValue((Mio_Gate_t *)pObj->pData) );
}
int Sfm_DecMarkMffc( Abc_Obj_t * pPivot, int nMffcMax, int fVeryVerbose )
{
    Abc_Obj_t * pFanin, * pFanin2;
    int i, k, nMffc = 1;
    pPivot->iTemp |= SFM_MASK_MFFC;
if ( fVeryVerbose )
printf( "Mffc = %d.\n", pPivot->Id );
    Abc_ObjForEachFanin( pPivot, pFanin, i )
        if ( Abc_ObjIsNode(pFanin) && Abc_ObjFanoutNum(pFanin) == 1 && Abc_NodeIsTravIdCurrent(pFanin) )
        {
            if ( nMffc == nMffcMax )
                return nMffc;
            pFanin->iTemp |= SFM_MASK_MFFC;
            nMffc++;
if ( fVeryVerbose )
printf( "Mffc = %d.\n", pFanin->Id );
        }
    Abc_ObjForEachFanin( pPivot, pFanin, i )
        if ( Abc_ObjIsNode(pFanin) && Abc_ObjFanoutNum(pFanin) == 1 && Abc_NodeIsTravIdCurrent(pFanin) )
        {
            if ( nMffc == nMffcMax )
                return nMffc;
            Abc_ObjForEachFanin( pFanin, pFanin2, k )
                if ( Abc_ObjIsNode(pFanin2) && Abc_ObjFanoutNum(pFanin2) == 1 && Abc_NodeIsTravIdCurrent(pFanin2) )
                {
                    if ( nMffc == nMffcMax )
                        return nMffc;
                    pFanin2->iTemp |= SFM_MASK_MFFC;
                    nMffc++;
if ( fVeryVerbose )
printf( "Mffc = %d.\n", pFanin2->Id );
                }
        }
    return nMffc;
}
int Abc_NtkDfsCheck_rec( Abc_Obj_t * pObj, Abc_Obj_t * pPivot )
{
    Abc_Obj_t * pFanin; int i;
    if ( pObj == pPivot )
        return 0;
    if ( Abc_NodeIsTravIdCurrent( pObj ) )
        return 1;
    Abc_NodeSetTravIdCurrent( pObj );
    if ( Abc_ObjIsCi(pObj) )
        return 1;
    assert( Abc_ObjIsNode(pObj) );
    Abc_ObjForEachFanin( pObj, pFanin, i )
        if ( !Abc_NtkDfsCheck_rec(pFanin, pPivot) )
            return 0;
    return 1;
}

int Sfm_DecExtract( Abc_Ntk_t * pNtk, Sfm_Par_t * pPars, Abc_Obj_t * pPivot, Vec_Int_t * vRoots, Vec_Int_t * vGates, Vec_Wec_t * vFanins, Vec_Int_t * vMap, Vec_Int_t * vTfi, Vec_Int_t * vTfo, int * pnMffc )
{
    Vec_Int_t * vLevel;
    Abc_Obj_t * pObj, * pFanin;
    int nLevelMax = pPivot->Level + pPars->nTfoLevMax;
    int nLevelMin = pPivot->Level - pPars->nTfiLevMax;
    int i, k, nTfiSize, nDivs = -1;
    assert( Abc_ObjIsNode(pPivot) );
if ( pPars->fVerbose )
printf( "\n\nTarget %d\n", Abc_ObjId(pPivot) );
    // collect TFO nodes
    Vec_IntClear( vTfo );
    Abc_NtkIncrementTravId( pNtk );
    Abc_NtkDfsReverseOne_rec( pPivot, vTfo, nLevelMax, pPars->nFanoutMax );
    // count internal fanouts
    Abc_NtkForEachObjVec( vTfo, pNtk, pObj, i )
        Abc_ObjForEachFanin( pObj, pFanin, k )
            pFanin->iTemp++;
    // comput roots
    Vec_IntClear( vRoots );
    Abc_NtkForEachObjVec( vTfo, pNtk, pObj, i )
        if ( pObj->iTemp != Abc_ObjFanoutNum(pObj) )
            Vec_IntPush( vRoots, Abc_ObjId(pObj) );
    assert( Vec_IntSize(vRoots) > 0 );
    // collect TFI and mark nodes
    Vec_IntClear( vTfi );
    Abc_NtkIncrementTravId( pNtk );
    Abc_NtkDfsOne_rec( pPivot, vTfi, nLevelMin, SFM_MASK_PI );
    nTfiSize = Vec_IntSize(vTfi);
    // additinally mark MFFC
    *pnMffc = Sfm_DecMarkMffc( pPivot, pPars->nMffcMax, pPars->fVeryVerbose );
    assert( *pnMffc <= pPars->nMffcMax );
if ( pPars->fVerbose )
printf( "Mffc size = %d.\n", *pnMffc );
    // collect TFI(TFO)
    Abc_NtkForEachObjVec( vTfo, pNtk, pObj, i )
        Abc_NtkDfsOne_rec( pObj, vTfi, nLevelMin, SFM_MASK_INPUT );
    // mark input-only nodes pointed to by mixed nodes
    Abc_NtkForEachObjVecStart( vTfi, pNtk, pObj, i, nTfiSize )
        if ( pObj->iTemp != SFM_MASK_INPUT )
            Abc_ObjForEachFanin( pObj, pFanin, k )
                if ( pFanin->iTemp == SFM_MASK_INPUT )
                    pFanin->iTemp = SFM_MASK_FANIN;
    // collect nodes supported only on TFI fanins and not MFFC
    Vec_IntClear( vMap );
    Vec_IntClear( vGates );
    Abc_NtkForEachObjVec( vTfi, pNtk, pObj, i )
        if ( pObj->iTemp == SFM_MASK_PI )
            Sfm_DecAddNode( pObj, vMap, vGates, Abc_ObjIsCi(pObj) || Abc_ObjLevel(pObj) < nLevelMin, pPars->fVeryVerbose );
    nDivs = Vec_IntSize(vMap);
if ( pPars->fVeryVerbose )
printf( "\nFinish divs\n" );
    // add other nodes that are not in TFO and not in MFFC
    Abc_NtkForEachObjVec( vTfi, pNtk, pObj, i )
        if ( pObj->iTemp == (SFM_MASK_PI | SFM_MASK_INPUT) || pObj->iTemp == SFM_MASK_FANIN || pObj->iTemp == 0 ) // const
            Sfm_DecAddNode( pObj, vMap, vGates, pObj->iTemp == SFM_MASK_FANIN, pPars->fVeryVerbose );
if ( pPars->fVeryVerbose )
printf( "\nFinish side\n" );
    // add the TFO nodes
    Abc_NtkForEachObjVec( vTfi, pNtk, pObj, i )
        if ( pObj->iTemp >= SFM_MASK_MFFC )
            Sfm_DecAddNode( pObj, vMap, vGates, 0, pPars->fVeryVerbose );
    assert( Vec_IntSize(vMap) == Vec_IntSize(vGates) );
if ( pPars->fVeryVerbose )
printf( "\nFinish all\n" );
    // create node IDs
    Vec_WecClear( vFanins );
    Abc_NtkForEachObjVec( vMap, pNtk, pObj, i )
    {
        pObj->iTemp = i;
        vLevel = Vec_WecPushLevel( vFanins );
        if ( Vec_IntEntry(vGates, i) >= 0 )
            Abc_ObjForEachFanin( pObj, pFanin, k )
                Vec_IntPush( vLevel, pFanin->iTemp );
    }
    // remap roots
    Abc_NtkForEachObjVec( vRoots, pNtk, pObj, i )
        Vec_IntWriteEntry( vRoots, i, pObj->iTemp );
/*
    // check
    Abc_NtkForEachObjVec( vMap, pNtk, pObj, i )
    {
        if ( i == nDivs )
            break;
        Abc_NtkIncrementTravId( pNtk );
        assert( Abc_NtkDfsCheck_rec(pObj, pPivot) );
    }
*/
    return nDivs;
}
void Sfm_DecInsert( Abc_Ntk_t * pNtk, Abc_Obj_t * pPivot, int Limit, Vec_Int_t * vGates, Vec_Wec_t * vFanins, Vec_Int_t * vMap, Vec_Ptr_t * vGateHandles )
{
    Abc_Obj_t * pObjNew = NULL; 
    int i, k, iObj, Gate;
    // assuming that new gates are appended at the end
    assert( Limit < Vec_IntSize(vGates) );
    assert( Limit == Vec_IntSize(vMap) );
    // introduce new gates
    Vec_IntForEachEntryStart( vGates, Gate, i, Limit )
    {
        Vec_Int_t * vLevel = Vec_WecEntry( vFanins, i );
        pObjNew = Abc_NtkCreateNode( pNtk );
        Vec_IntForEachEntry( vLevel, iObj, k )
            Abc_ObjAddFanin( pObjNew, Abc_NtkObj(pNtk, Vec_IntEntry(vMap, iObj)) );
        pObjNew->pData = Vec_PtrEntry( vGateHandles, Gate );
        Vec_IntPush( vMap, Abc_ObjId(pObjNew) );
    }
    // transfer the fanout
    Abc_ObjTransferFanout( pPivot, pObjNew );
    assert( Abc_ObjFanoutNum(pPivot) == 0 );
    Abc_NtkDeleteObj_rec( pPivot, 1 );
    // update level
    Abc_NtkForEachObjVecStart( vMap, pNtk, pObjNew, i, Limit )
        Abc_NtkUpdateIncLevel( pObjNew );
}
void Abc_NtkPerformMfs3( Abc_Ntk_t * pNtk, Sfm_Par_t * pPars )
{
    extern void Sfm_LibPreprocess( Mio_Library_t * pLib, Vec_Int_t * vGateSizes, Vec_Wrd_t * vGateFuncs, Vec_Wec_t * vGateCnfs, Vec_Ptr_t * vGateHands );
    Mio_Library_t * pLib = (Mio_Library_t *)pNtk->pManFunc;
    Sfm_Dec_t * p = Sfm_DecStart( pPars );
    Abc_Obj_t * pObj; 
    int i, Limit, Count = 0, nStop = Abc_NtkObjNumMax(pNtk);
    //int iNode = 2299;//8;//70;
    printf( "Running remapping with parameters: " );
    printf( "TFO = %d. ", pPars->nTfoLevMax );
    printf( "TFI = %d. ", pPars->nTfiLevMax );
    printf( "FanMax = %d. ", pPars->nFanoutMax );
    printf( "MffcMax = %d. ", pPars->nMffcMax );
    printf( "\n" );
    // enter library
    assert( Abc_NtkIsMappedLogic(pNtk) );
    Sfm_LibPreprocess( pLib, &p->vGateSizes, &p->vGateFuncs, &p->vGateCnfs, &p->vGateHands );
    p->GateConst0 = Mio_GateReadValue( Mio_LibraryReadConst0(pLib) );
    p->GateConst1 = Mio_GateReadValue( Mio_LibraryReadConst1(pLib) );
    p->GateBuffer = Mio_GateReadValue( Mio_LibraryReadBuf(pLib) );
    p->GateInvert = Mio_GateReadValue( Mio_LibraryReadInv(pLib) );
    p->GateAnd[0] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "and00", NULL) );
    p->GateAnd[1] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "and01", NULL) );
    p->GateAnd[2] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "and10", NULL) );
    p->GateAnd[3] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "and11", NULL) );
    p->GateOr[0] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "or00", NULL) );
    p->GateOr[1] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "or01", NULL) );
    p->GateOr[2] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "or10", NULL) );
    p->GateOr[3] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "or11", NULL) );
    // iterate over nodes
    Abc_NtkLevel( pNtk );
    Abc_NtkForEachNode( pNtk, pObj, i )
//    for ( ; (pObj = Abc_NtkObj(pNtk, iNode));  )
    {
        if ( i >= nStop )
            break;
        //if ( Abc_ObjFaninNum(pObj) == 0 || (Abc_ObjFaninNum(pObj) == 1 && Abc_ObjFanoutNum(Abc_ObjFanin0(pObj)) > 1) )
        //    continue;
        p->nDivs = Sfm_DecExtract( pNtk, pPars, pObj, &p->vObjRoots, &p->vObjGates, &p->vObjFanins, &p->vObjMap, &p->vTemp, &p->vTemp2, &p->nMffc );
        p->iTarget = pObj->iTemp;
        Limit = Vec_IntSize( &p->vObjGates );
        if ( !Sfm_DecPrepareSolver( p ) )
            continue;
        if ( Sfm_DecPeformDec( p, pLib ) == -1 )
            continue;
        Sfm_DecInsert( pNtk, pObj, Limit, &p->vObjGates, &p->vObjFanins, &p->vObjMap, &p->vGateHands );
if ( pPars->fVerbose )
printf( "This was modification %d\n", Count );
        //if ( Count == 2 )
        //    break;
        Count++;
    }
    Sfm_DecStop( p );
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

