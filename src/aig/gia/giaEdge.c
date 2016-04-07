/**CFile****************************************************************

  FileName    [giaEdge.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Scalable AIG package.]

  Synopsis    [Edge-related procedures.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: giaEdge.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "gia.h"

ABC_NAMESPACE_IMPL_START

////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

static inline int Gia_ObjEdgeCount( int iObj, Vec_Int_t * vEdge1, Vec_Int_t * vEdge2 )
{
    return (Vec_IntEntry(vEdge1, iObj) > 0) + (Vec_IntEntry(vEdge2, iObj) > 0);
}
static inline int Gia_ObjEdgeAdd( int iObj, int iNext, Vec_Int_t * vEdge1, Vec_Int_t * vEdge2 )
{
    int RetValue = 0;
    if ( Vec_IntEntry(vEdge1, iObj) == 0 )
        Vec_IntWriteEntry(vEdge1, iObj, iNext);
    else if ( Vec_IntEntry(vEdge2, iObj) == 0 )
        Vec_IntWriteEntry(vEdge2, iObj, iNext);
    else RetValue = 1;
    return RetValue;
}
static inline void Gia_ObjEdgeRemove( int iObj, int iNext, Vec_Int_t * vEdge1, Vec_Int_t * vEdge2 )
{
    assert( Vec_IntEntry(vEdge1, iObj) == iNext || Vec_IntEntry(vEdge2, iObj) == iNext );
    if ( Vec_IntEntry(vEdge1, iObj) == iNext )
        Vec_IntWriteEntry( vEdge1, iObj, Vec_IntEntry(vEdge2, iObj) );
    Vec_IntWriteEntry( vEdge2, iObj, 0 );
}
static inline void Gia_ObjEdgeClean( int iObj, Vec_Int_t * vEdge1, Vec_Int_t * vEdge2 )
{
    Vec_IntWriteEntry( vEdge1, iObj, 0 );
    Vec_IntWriteEntry( vEdge2, iObj, 0 );
}

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Transforms edge assignment.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManEdgeFromArray( Gia_Man_t * p, Vec_Int_t * vArray )
{
    int i, iObj1, iObj2;
    Vec_IntFreeP( &p->vEdge1 );
    Vec_IntFreeP( &p->vEdge2 );
    p->vEdge1 = Vec_IntStart( Gia_ManObjNum(p) );
    p->vEdge2 = Vec_IntStart( Gia_ManObjNum(p) );
    Vec_IntForEachEntryDouble( vArray, iObj1, iObj2, i )
    {
        assert( iObj1 < iObj2 );
        Gia_ObjEdgeAdd( iObj1, iObj2, p->vEdge1, p->vEdge2 );
        Gia_ObjEdgeAdd( iObj2, iObj1, p->vEdge1, p->vEdge2 );
    }
}
Vec_Int_t * Gia_ManEdgeToArray( Gia_Man_t * p )
{
    int iObj, iFanin;
    Vec_Int_t * vArray = Vec_IntAlloc( 1000 );
    assert( p->vEdge1 && p->vEdge2 );
    assert( Vec_IntSize(p->vEdge1) == Gia_ManObjNum(p) );
    assert( Vec_IntSize(p->vEdge2) == Gia_ManObjNum(p) );
    for ( iObj = 0; iObj < Gia_ManObjNum(p); iObj++ )
    {
        iFanin = Vec_IntEntry( p->vEdge1, iObj );
        if ( iFanin && iFanin < iObj )
            Vec_IntPushTwo( vArray, iFanin, iObj );
        iFanin = Vec_IntEntry( p->vEdge2, iObj );
        if ( iFanin && iFanin < iObj )
            Vec_IntPushTwo( vArray, iFanin, iObj );
    }
    return vArray;
}

/**Function*************************************************************

  Synopsis    [Prints mapping statistics.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManConvertPackingToEdges( Gia_Man_t * p )
{
    int i, k, Entry, nEntries, nEntries2, nNodes[4], Count = 0;
    if ( p->vPacking == NULL )
        return;
    Vec_IntFreeP( &p->vEdge1 );
    Vec_IntFreeP( &p->vEdge2 );
    p->vEdge1 = Vec_IntStart( Gia_ManObjNum(p) );
    p->vEdge2 = Vec_IntStart( Gia_ManObjNum(p) );
    // iterate through structures
    nEntries = Vec_IntEntry( p->vPacking, 0 );
    nEntries2 = 0;
    Vec_IntForEachEntryStart( p->vPacking, Entry, i, 1 )
    {
        assert( Entry > 0 && Entry < 4 );
        i++;
        for ( k = 0; k < Entry; k++, i++ )
            nNodes[k] = Vec_IntEntry(p->vPacking, i);
        i--;
        nEntries2++;
        // create edges
        if ( Entry == 2 )
        {
            Count += Gia_ObjEdgeAdd( nNodes[0], nNodes[1], p->vEdge1, p->vEdge2 );
            Count += Gia_ObjEdgeAdd( nNodes[1], nNodes[0], p->vEdge1, p->vEdge2 );
        }
        else if ( Entry == 3 )
        {
            Count += Gia_ObjEdgeAdd( nNodes[0], nNodes[2], p->vEdge1, p->vEdge2 );
            Count += Gia_ObjEdgeAdd( nNodes[2], nNodes[0], p->vEdge1, p->vEdge2 );
            Count += Gia_ObjEdgeAdd( nNodes[1], nNodes[2], p->vEdge1, p->vEdge2 );
            Count += Gia_ObjEdgeAdd( nNodes[2], nNodes[1], p->vEdge1, p->vEdge2 );
        }
    }
    assert( nEntries == nEntries2 );
    printf( "Skipped %d illegal edges.\n", Count );
}

/**Function*************************************************************

  Synopsis    [Evaluates given edge assignment.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline int Gia_ObjHaveEdge( Gia_Man_t * p, int iObj, int iNext )
{
    return Vec_IntEntry(p->vEdge1, iObj) == iNext || Vec_IntEntry(p->vEdge2, iObj) == iNext;
}
static inline int Gia_ObjEvalEdgeDelay( Gia_Man_t * p, int iObj, Vec_Int_t * vDelay )
{
    int i, iFan, Delay, DelayMax = 0;
    if ( Gia_ManHasMapping(p) && Gia_ObjIsLut(p, iObj) )
    {
        assert( Gia_ObjLutSize(p, iObj) <= 4 );
        Gia_LutForEachFanin( p, iObj, iFan, i )
        {
            Delay = Vec_IntEntry(vDelay, iFan) + !Gia_ObjHaveEdge(p, iObj, iFan);
            DelayMax = Abc_MaxInt( DelayMax, Delay );
        }
    }
    else if ( Gia_ObjIsLut2(p, iObj) )
    {
        assert( Gia_ObjLutSize2(p, iObj) <= 4 );
        Gia_LutForEachFanin2( p, iObj, iFan, i )
        {
            Delay = Vec_IntEntry(vDelay, iFan) + !Gia_ObjHaveEdge(p, iObj, iFan);
            DelayMax = Abc_MaxInt( DelayMax, Delay );
        }
    }
    else assert( 0 );
    return DelayMax;
}
int Gia_ManEvalEdgeDelay( Gia_Man_t * p )
{
    int k, iLut, DelayMax = 0;
    assert( p->vEdge1 && p->vEdge2 );
    Vec_IntFreeP( &p->vEdgeDelay );
    p->vEdgeDelay = Vec_IntStart( Gia_ManObjNum(p) );
    if ( Gia_ManHasMapping(p) )
        Gia_ManForEachLut( p, iLut )
            Vec_IntWriteEntry( p->vEdgeDelay, iLut, Gia_ObjEvalEdgeDelay(p, iLut, p->vEdgeDelay) );
    else if ( Gia_ManHasMapping2(p) )
        Gia_ManForEachLut2( p, iLut )
            Vec_IntWriteEntry( p->vEdgeDelay, iLut, Gia_ObjEvalEdgeDelay(p, iLut, p->vEdgeDelay) );
    else assert( 0 );
    Gia_ManForEachCoDriverId( p, iLut, k )
        DelayMax = Abc_MaxInt( DelayMax, Vec_IntEntry(p->vEdgeDelay, iLut) );
    return DelayMax;
}
int Gia_ManEvalEdgeCount( Gia_Man_t * p )
{
    return (Vec_IntCountPositive(p->vEdge1) + Vec_IntCountPositive(p->vEdge2))/2;
}


/**Function*************************************************************

  Synopsis    [Finds edge assignment.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ObjComputeEdgeDelay( Gia_Man_t * p, int iObj, Vec_Int_t * vDelay, Vec_Int_t * vEdge1, Vec_Int_t * vEdge2 )
{
    int i, iFan, Delay, Status1, Status2;
    int DelayMax = 0, nCountMax = 0;
    int iFanMax1 = -1, iFanMax2 = -1;
    Vec_IntWriteEntry(vEdge1, iObj, 0);
    Vec_IntWriteEntry(vEdge2, iObj, 0);
    if ( Gia_ManHasMapping(p) && Gia_ObjIsLut(p, iObj) )
    {
        assert( Gia_ObjLutSize(p, iObj) <= 4 );
        Gia_LutForEachFanin( p, iObj, iFan, i )
        {
            Delay = Vec_IntEntry( vDelay, iFan ) + 1;
            if ( DelayMax < Delay )
            {
                DelayMax  = Delay;
                iFanMax1  = iFan;
                nCountMax = 1;
            }
            else if ( DelayMax == Delay )
            {
                iFanMax2  = iFan;
                nCountMax++;
            }
        }
    }
    else if ( Gia_ObjIsLut2(p, iObj) )
    {
        assert( Gia_ObjLutSize2(p, iObj) <= 4 );
        Gia_LutForEachFanin2( p, iObj, iFan, i )
        {
            Delay = Vec_IntEntry( vDelay, iFan ) + 1;
            if ( DelayMax < Delay )
            {
                DelayMax  = Delay;
                iFanMax1  = iFan;
                nCountMax = 1;
            }
            else if ( DelayMax == Delay )
            {
                iFanMax2  = iFan;
                nCountMax++;
            }
        }
    }
    else assert( 0 );
    assert( nCountMax > 0 );
    if ( DelayMax == 1 )
    {} // skip first level
    else if ( nCountMax == 1 )
    {
        Status1 = Gia_ObjEdgeCount( iFanMax1, vEdge1, vEdge2 );
        if ( Status1 <= 1 )
        {
            Gia_ObjEdgeAdd( iFanMax1, iObj, vEdge1, vEdge2 );
            Gia_ObjEdgeAdd( iObj, iFanMax1, vEdge1, vEdge2 );
            Vec_IntWriteEntry( vDelay, iObj, DelayMax-1 );
            return DelayMax-1;
        }
    }
    else if ( nCountMax == 2 )
    {
        Status1 = Gia_ObjEdgeCount( iFanMax1, vEdge1, vEdge2 );
        Status2 = Gia_ObjEdgeCount( iFanMax2, vEdge1, vEdge2 );
        if ( Status1 <= 1 && Status2 <= 1 )
        {
            Gia_ObjEdgeAdd( iFanMax1, iObj, vEdge1, vEdge2 );
            Gia_ObjEdgeAdd( iFanMax2, iObj, vEdge1, vEdge2 );
            Gia_ObjEdgeAdd( iObj, iFanMax1, vEdge1, vEdge2 );
            Gia_ObjEdgeAdd( iObj, iFanMax2, vEdge1, vEdge2 );
            Vec_IntWriteEntry( vDelay, iObj, DelayMax-1 );
            return DelayMax-1;
        }
    }
    Vec_IntWriteEntry( vDelay, iObj, DelayMax );
    return DelayMax;
}
int Gia_ManComputeEdgeDelay( Gia_Man_t * p )
{
    int k, iLut, DelayMax = 0;
    Vec_IntFreeP( &p->vEdgeDelay );
    Vec_IntFreeP( &p->vEdge1 );
    Vec_IntFreeP( &p->vEdge2 );
    p->vEdge1 = Vec_IntStart( Gia_ManObjNum(p) );
    p->vEdge2 = Vec_IntStart( Gia_ManObjNum(p) );
    p->vEdgeDelay = Vec_IntStart( Gia_ManObjNum(p) );
    if ( Gia_ManHasMapping(p) )
        Gia_ManForEachLut( p, iLut )
            Gia_ObjComputeEdgeDelay( p, iLut, p->vEdgeDelay, p->vEdge1, p->vEdge2 );
    else if ( Gia_ManHasMapping2(p) )
        Gia_ManForEachLut2( p, iLut )
            Gia_ObjComputeEdgeDelay( p, iLut, p->vEdgeDelay, p->vEdge1, p->vEdge2 );
    else assert( 0 );
    Gia_ManForEachCoDriverId( p, iLut, k )
        DelayMax = Abc_MaxInt( DelayMax, Vec_IntEntry(p->vEdgeDelay, iLut) );
    //printf( "The number of edges = %d.  Delay = %d.\n", Gia_ManEvalEdgeCount(p), DelayMax );
    return DelayMax;
}

/**Function*************************************************************

  Synopsis    [Finds edge assignment.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Gia_ObjComputeEdgeDelay2( Gia_Man_t * p, int iObj, Vec_Int_t * vDelay, Vec_Int_t * vEdge1, Vec_Int_t * vEdge2, Vec_Int_t * vFanMax1, Vec_Int_t * vFanMax2, Vec_Int_t * vCountMax )
{
    int i, iFan, DelayFanin, Status1, Status2;
    int DelayMax = 0, nCountMax = 0;
    int iFanMax1 = -1, iFanMax2 = -1;
    Vec_IntWriteEntry(vEdge1, iObj, 0);
    Vec_IntWriteEntry(vEdge2, iObj, 0);
    // analyze this node
    DelayMax = Vec_IntEntry( vDelay, iObj );
    nCountMax = Vec_IntEntry( vCountMax, iObj );
    if ( DelayMax == 0 )
    {}
    else if ( nCountMax == 1 )
    {
        iFanMax1 = Vec_IntEntry( vFanMax1, iObj );
        Status1 = Gia_ObjEdgeCount( iFanMax1, vEdge1, vEdge2 );
        if ( Status1 <= 1 )
        {
            Gia_ObjEdgeAdd( iFanMax1, iObj, vEdge1, vEdge2 );
            Gia_ObjEdgeAdd( iObj, iFanMax1, vEdge1, vEdge2 );
            DelayMax--;
        }
    }
    else if ( nCountMax == 2 )
    {
        iFanMax1 = Vec_IntEntry( vFanMax1, iObj );
        iFanMax2 = Vec_IntEntry( vFanMax2, iObj );
        Status1 = Gia_ObjEdgeCount( iFanMax1, vEdge1, vEdge2 );
        Status2 = Gia_ObjEdgeCount( iFanMax2, vEdge1, vEdge2 );
        if ( Status1 <= 1 && Status2 <= 1 )
        {
            Gia_ObjEdgeAdd( iFanMax1, iObj, vEdge1, vEdge2 );
            Gia_ObjEdgeAdd( iFanMax2, iObj, vEdge1, vEdge2 );
            Gia_ObjEdgeAdd( iObj, iFanMax1, vEdge1, vEdge2 );
            Gia_ObjEdgeAdd( iObj, iFanMax2, vEdge1, vEdge2 );
            DelayMax--;
        }
    }
    Vec_IntWriteEntry( vDelay, iObj, DelayMax );
    // computed DelayMax at this point
    if ( Gia_ManHasMapping(p) && Gia_ObjIsLut(p, iObj) )
    {
        Gia_LutForEachFanin( p, iObj, iFan, i )
        {
            DelayFanin = Vec_IntEntry( vDelay, iFan );
            if ( DelayFanin < DelayMax + 1 )
            {
                Vec_IntWriteEntry( vDelay, iFan, DelayMax + 1 );
                Vec_IntWriteEntry( vFanMax1, iFan, iObj );
                Vec_IntWriteEntry( vCountMax, iFan, 1 );
            }
            else if ( DelayFanin == DelayMax + 1 )
            {
                Vec_IntWriteEntry( vFanMax2, iFan, iObj );
                Vec_IntAddToEntry( vCountMax, iFan, 1 );
            }
        }
    }
    else if ( Gia_ObjIsLut2(p, iObj) )
    {
        Gia_LutForEachFanin2( p, iObj, iFan, i )
        {
            DelayFanin = Vec_IntEntry( vDelay, iFan );
            if ( DelayFanin < DelayMax + 1 )
            {
                Vec_IntWriteEntry( vDelay, iFan, DelayMax + 1 );
                Vec_IntWriteEntry( vFanMax1, iFan, iObj );
                Vec_IntWriteEntry( vCountMax, iFan, 1 );
            }
            else if ( DelayFanin == DelayMax + 1 )
            {
                Vec_IntWriteEntry( vFanMax2, iFan, iObj );
                Vec_IntAddToEntry( vCountMax, iFan, 1 );
            }
        }
    }
    else assert( 0 );
    return DelayMax;
}
int Gia_ManComputeEdgeDelay2( Gia_Man_t * p )
{
    int k, iLut, DelayMax = 0, EdgeCount = 0;
    Vec_Int_t * vFanMax1 = Vec_IntStart( Gia_ManObjNum(p) );
    Vec_Int_t * vFanMax2 = Vec_IntStart( Gia_ManObjNum(p) );
    Vec_Int_t * vCountMax = Vec_IntStart( Gia_ManObjNum(p) );
    Vec_IntFreeP( &p->vEdgeDelay );
    Vec_IntFreeP( &p->vEdge1 );
    Vec_IntFreeP( &p->vEdge2 );
    p->vEdgeDelay = Vec_IntStart( Gia_ManObjNum(p) );
    p->vEdge1 = Vec_IntStart( Gia_ManObjNum(p) );
    p->vEdge2 = Vec_IntStart( Gia_ManObjNum(p) );
//    Gia_ManForEachCoDriverId( p, iLut, k )
//        Vec_IntWriteEntry( p->vEdgeDelay, iLut, 1 );
    if ( Gia_ManHasMapping(p) )
        Gia_ManForEachLutReverse( p, iLut )
            Gia_ObjComputeEdgeDelay2( p, iLut, p->vEdgeDelay, p->vEdge1, p->vEdge2, vFanMax1, vFanMax2, vCountMax );
    else if ( Gia_ManHasMapping2(p) )
        Gia_ManForEachLut2Reverse( p, iLut )
            Gia_ObjComputeEdgeDelay2( p, iLut, p->vEdgeDelay, p->vEdge1, p->vEdge2, vFanMax1, vFanMax2, vCountMax );
    else assert( 0 );
    Gia_ManForEachCiId( p, iLut, k )
        DelayMax = Abc_MaxInt( DelayMax, Vec_IntEntry(p->vEdgeDelay, iLut) );
    Vec_IntFree( vFanMax1 );
    Vec_IntFree( vFanMax2 );
    Vec_IntFree( vCountMax );
    //printf( "The number of edges = %d.  Delay = %d.\n", Gia_ManEvalEdgeCount(p), DelayMax );
    return DelayMax;
}

/**Function*************************************************************

  Synopsis    [Finds edge assignment.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Gia_ManUpdateMapping( Gia_Man_t * p, Vec_Int_t * vNodes, Vec_Wec_t * vWin )
{
    int i, iNode;
    Vec_IntForEachEntry( vNodes, iNode, i )
        ABC_SWAP( Vec_Int_t, *Vec_WecEntry(p->vMapping2, iNode), *Vec_WecEntry(vWin, i) );
}
int Gia_ManEvalWindowInc( Gia_Man_t * p, Vec_Int_t * vLeaves, Vec_Int_t * vNodes, Vec_Wec_t * vWin, Vec_Int_t * vTemp )
{
    int i, iLut, Delay, DelayMax = 0;
    assert( Vec_IntSize(vNodes) == Vec_WecSize(vWin) );
    Gia_ManUpdateMapping( p, vNodes, vWin );
    Gia_ManCollectTfo( p, vLeaves, vTemp );
    Vec_IntReverseOrder( vTemp );
    Vec_IntForEachEntry( vTemp, iLut, i )
    {
        if ( !Gia_ObjIsLut(p, iLut) )
            continue;
        Delay = Gia_ObjComputeEdgeDelay( p, iLut, p->vEdgeDelay, p->vEdge1, p->vEdge2 );
        DelayMax = Abc_MaxInt( DelayMax, Delay );
    }
    Gia_ManUpdateMapping( p, vNodes, vWin );
    return DelayMax;
}
int Gia_ManEvalWindow( Gia_Man_t * p, Vec_Int_t * vLeaves, Vec_Int_t * vNodes, Vec_Wec_t * vWin, Vec_Int_t * vTemp )
{
    int DelayMax;
    assert( Vec_IntSize(vNodes) == Vec_WecSize(vWin) );
    Gia_ManUpdateMapping( p, vNodes, vWin );
    DelayMax = Gia_ManComputeEdgeDelay( p );
    Gia_ManUpdateMapping( p, vNodes, vWin );
    return DelayMax;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

