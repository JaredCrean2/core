/****************************************************************************** 

  Copyright 2013 Scientific Computation Research Center, 
      Rensselaer Polytechnic Institute. All rights reserved.
  
  The LICENSE file included with this distribution describes the terms
  of the SCOREC Non-Commercial License this program is distributed under.
 
*******************************************************************************/
#include <PCU.h>
#include "maShape.h"
#include "maSize.h"
#include "maAdapt.h"
#include "maOperator.h"
#include "maEdgeSwap.h"
#include "maDoubleSplitCollapse.h"
#include "maShortEdgeRemover.h"
#include "maShapeHandler.h"

namespace ma {

/* projects vertex 3 onto the plane
   of the bottom triangle and returns
   the zone in which it lands as a bit code.
   Each bit indicates whether the area coordinate
   of that vertex is positive.
*/

int getSliverCode(
    Adapt* a,
    Entity* tet)
{
  SizeField* sf = a->sizeField;
  Mesh* m = a->mesh;
  Matrix J,Q;
  apf::MeshElement* me = apf::createMeshElement(m,tet);
  Vector center(.25,.25,.25);
  apf::getJacobian(me,center,J);
  sf->getTransform(me,center,Q);
  J = J*Q; //Jacobian in metric space
  apf::destroyMeshElement(me);
  Vector v03 = J[2];
  J[2] = apf::cross(J[0],J[1]); //face normal towards v[3]
  Vector projected = v03 - apf::project(v03,J[2]); //v[3] projected to face
  Matrix inverseMap = apf::invert(apf::transpose(J));
  Vector basisPoint = inverseMap * projected;
  Vector areaPoint(1-basisPoint[0]-basisPoint[1],
                   basisPoint[0],
                   basisPoint[1]);
  int code = 0;
  for (int i=0; i < 3; ++i)
    if (areaPoint[i] > 0)
      code |= (1<<i);
  assert(code);
  return code;
}

CodeMatch matchSliver(
    Adapt* a,
    Entity* tet)
{
  /* this table was auto-generated by the sliverCodeMatch program */
  CodeMatch const table[8] =
  {{-1,-1}
  ,{4,1}
  ,{1,1}
  ,{2,0}
  ,{2,1}
  ,{0,0}
  ,{1,0}
  ,{0,1}
  };
  return table[getSliverCode(a,tet)];
}

struct IsBadQuality : public Predicate
{
  IsBadQuality(Adapt* a_):a(a_) {}
  bool operator()(Entity* e)
  {
    return a->shape->getQuality(e) < a->input->goodQuality;
  }
  Adapt* a;
};

int markBadQuality(Adapt* a)
{
  IsBadQuality p(a);
  return markEntities(a, a->mesh->getDimension(), p, BAD_QUALITY, OK_QUALITY);
}

class ShortEdgeFixer : public Operator
{
  public:
    ShortEdgeFixer(Adapt* a):
      remover(a)
    {
      adapter = a;
      mesh = a->mesh;
      sizeField = a->sizeField;
      shortEdgeRatio = a->input->maximumEdgeRatio;
      nr = nf = 0;
    }
    virtual ~ShortEdgeFixer()
    {
    }
    virtual int getTargetDimension() {return mesh->getDimension();}
    virtual bool shouldApply(Entity* e)
    {
      if ( ! getFlag(adapter,e,BAD_QUALITY))
        return false;
      element = e;
      Downward edges;
      int n = mesh->getDownward(element,1,edges);
      double l[6] = {};
      for (int i=0; i < n; ++i)
        l[i] = sizeField->measure(edges[i]);
      double maxLength;
      double minLength;
      Entity* shortEdge;
      maxLength = minLength = l[0];
      shortEdge = edges[0];
      for (int i=1; i < n; ++i)
      {
        if (l[i] > maxLength) maxLength = l[i];
        if (l[i] < minLength)
        {
          minLength = l[i];
          shortEdge = edges[i];
        }
      }
      if ((maxLength/minLength) < shortEdgeRatio)
      {
        clearFlag(adapter,element,BAD_QUALITY);
        return false;
      }
      remover.setEdge(shortEdge);
      return true;
    }
    virtual bool requestLocality(apf::CavityOp* o)
    {
      return remover.requestLocality(o);
    }
    virtual void apply()
    {
      if (remover.run())
        ++nr;
      else
      {
        ++nf;
        clearFlag(adapter,element,BAD_QUALITY);
      }
    }
  private:
    Adapt* adapter;
    Mesh* mesh;
    Entity* element;
    SizeField* sizeField;
    ShortEdgeRemover remover;
    double shortEdgeRatio;
    int nr;
    int nf;
};

class TetFixerBase
{
  public:
    virtual void setTet(Entity** v) = 0;
    virtual bool requestLocality(apf::CavityOp* o) = 0;
    virtual bool run() = 0;
};

class FaceVertFixer : public TetFixerBase
{
  public:
    FaceVertFixer(Adapt* a)
    {
      mesh = a->mesh;
      edgeSwap = makeEdgeSwap(a);
      nes = nf = 0;
    }
    ~FaceVertFixer()
    {
      delete edgeSwap;
    }
    virtual void setTet(Entity** v)
    {
/* in this template, the bottom face and v[3]
   are too close, the key edges are those that bound
   face v(0,1,2) */
      apf::findTriDown(mesh,v,edges);
    }
    virtual bool requestLocality(apf::CavityOp* o)
    {
      return o->requestLocality(edges,3);
    }
    virtual bool run()
    {
      for (int i=0; i < 3; ++i)
        if (edgeSwap->run(edges[i]))
        {
          ++nes;
          return true;
        }
      ++nf;
      return false;
    }
  private:
    Mesh* mesh;
    Entity* edges[3];
    EdgeSwap* edgeSwap;
    int nes;
    int nf;
};

class EdgeEdgeFixer : public TetFixerBase
{
  public:
    EdgeEdgeFixer(Adapt* a):
      doubleSplitCollapse(a)
    {
      mesh = a->mesh;
      edgeSwap = makeEdgeSwap(a);
      nes = ndsc = nf = 0;
      sf = a->sizeField;
    }
    ~EdgeEdgeFixer()
    {
      delete edgeSwap;
    }
    virtual void setTet(Entity** v)
    {
/* in this template, the v[0]-v[2] amd v[1]-v[3]
   edges are too close. */
      Entity* ev[2];
      ev[0] = v[0]; ev[1] = v[2];
      edges[0] = findUpward(mesh,EDGE,ev);
      ev[0] = v[1]; ev[1] = v[3];
      edges[1] = findUpward(mesh,EDGE,ev);
    }
    virtual bool requestLocality(apf::CavityOp* o)
    {
      return o->requestLocality(edges,2);
    }
    virtual bool run()
    {
      for (int i=0; i < 2; ++i)
        if (edgeSwap->run(edges[i]))
        {
          ++nes;
          return true;
        }
      if (doubleSplitCollapse.run(edges))
      {
        ++ndsc;
        return true;
      }
      ++nf;
      return false;
    }
  private:
    Mesh* mesh;
    Entity* edges[2];
    EdgeSwap* edgeSwap;
    DoubleSplitCollapse doubleSplitCollapse;
    int nes;
    int ndsc;
    int nf;
    SizeField* sf;
};

class LargeAngleTetFixer : public Operator
{
  public:
    LargeAngleTetFixer(Adapt* a):
      edgeEdgeFixer(a),
      faceVertFixer(a)
    {
      adapter = a;
      mesh = a->mesh;
    }
    virtual ~LargeAngleTetFixer()
    {
    }
    virtual int getTargetDimension() {return 3;}
    enum { EDGE_EDGE, FACE_VERT };
    virtual bool shouldApply(Entity* e)
    {
      if ( ! getFlag(adapter,e,BAD_QUALITY))
        return false;
      tet = e;
      CodeMatch match = matchSliver(adapter,e);
      if (match.code_index==EDGE_EDGE)
        fixer = &edgeEdgeFixer;
      else
      { assert(match.code_index==FACE_VERT);
        fixer = &faceVertFixer;
      }
      Entity* v[4];
      mesh->getDownward(e,0,v);
      Entity* rv[4];
      rotateTet(v,match.rotation,rv);
      fixer->setTet(rv);
      return true;
    }
    virtual bool requestLocality(apf::CavityOp* o)
    {
      return fixer->requestLocality(o);
    }
    virtual void apply()
    {
      if ( ! fixer->run())
        clearFlag(adapter,tet,BAD_QUALITY);
    }
  private:
    Adapt* adapter;
    Mesh* mesh;
    Entity* tet;
    EdgeEdgeFixer edgeEdgeFixer;
    FaceVertFixer faceVertFixer;
    TetFixerBase* fixer;
};

class LargeAngleTriFixer : public Operator
{
  public:
    LargeAngleTriFixer(Adapt* a)
    {
      adapter = a;
      mesh = a->mesh;
      edgeSwap = makeEdgeSwap(a);
      ns = nf = 0;
    }
    virtual ~LargeAngleTriFixer()
    {
      delete edgeSwap;
    }
    virtual int getTargetDimension() {return 2;}
    virtual bool shouldApply(Entity* e)
    {
      if ( ! getFlag(adapter,e,BAD_QUALITY))
        return false;
      tri = e;
      mesh->getDownward(e,1,edges);
      return true;
    }
    virtual bool requestLocality(apf::CavityOp* o)
    {
      return o->requestLocality(edges,3);
    }
    virtual void apply()
    {
      for (int i=0; i < 3; ++i)
        if (edgeSwap->run(edges[i]))
        {
          ++ns;
          return;
        }
      ++nf;
      clearFlag(adapter,tri,BAD_QUALITY);
    }
  private:
    Adapt* adapter;
    Mesh* mesh;
    Entity* tri;
    Entity* edges[3];
    EdgeSwap* edgeSwap;
    int ns;
    int nf;
};

static void fixShortEdgeElements(Adapt* a)
{
  ShortEdgeFixer fixer(a);
  applyOperator(a,&fixer);
}

static void fixLargeAngleTets(Adapt* a)
{
  LargeAngleTetFixer fixer(a);
  applyOperator(a,&fixer);
}

static void fixLargeAngleTris(Adapt* a)
{
  LargeAngleTriFixer fixer(a);
  applyOperator(a,&fixer);
}

static void fixLargeAngles(Adapt* a)
{
  if (a->mesh->getDimension()==3)
    fixLargeAngleTets(a);
  else
    fixLargeAngleTris(a);
}

void fixElementShapes(Adapt* a)
{
  if ( ! a->input->shouldFixShape)
    return;
  double t0 = MPI_Wtime();
  int count = markBadQuality(a);
  int originalCount = count;
  int prev_count;
  do {
    if ( ! count)
      break;
    prev_count = count;
    fixLargeAngles(a);
    markBadQuality(a);
    fixShortEdgeElements(a);
    count = markBadQuality(a);
  } while(count < prev_count);
  double t1 = MPI_Wtime();
  print("bad shapes down from %d to %d in %f seconds",
        originalCount,count,t1-t0);
}

}
