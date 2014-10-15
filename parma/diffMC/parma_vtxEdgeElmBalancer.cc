#include <PCU.h>
#include <apfPartition.h>
#include "parma_base.h"
#include "parma_sides.h"
#include "parma_weights.h"
#include "parma_targets.h"
#include "parma_selector.h"
#include "parma_centroids.h"

namespace parma {
  class VtxEdgeElmBalancer : public apf::Balancer {
    public:
      VtxEdgeElmBalancer(apf::Mesh* m, double f, int v)
        : mesh(m), factor(f), verbose(v) {
          (void) verbose; // silence!
      }
      bool runVtxStep(apf::MeshTag* wtag, double tolerance) {
        Sides* s = makeElmBdrySides(mesh);
        Weights* w = makeEntWeights(mesh, wtag, s, 0);
        Targets* t = makeTargets(s, w, factor);
        Selector* sel = makeVtxSelector(mesh, wtag);
        parma::Balancer parmaVtx(mesh, wtag, factor, s, w, t, sel);
        return parmaVtx.run(tolerance);
      }
      bool runEdgeStep(apf::MeshTag* wtag, double tolerance) {
        Sides* s = makeElmBdrySides(mesh);
        Weights* w[2] = 
          {makeEntWeights(mesh, wtag, s, 0), 
           makeEntWeights(mesh, wtag, s, 1)};
        Targets* t = makeVtxEdgeTargets(s, w, factor);
        Selector* sel = makeEdgeSelector(mesh, wtag);
        parma::Balancer parmaVtx(mesh, wtag, factor, s, w[0], t, sel);
        return parmaVtx.run(tolerance);
      }
      bool runElmStep(apf::MeshTag* wtag, double tolerance) {
        Sides* s = makeElmBdrySides(mesh);
        Weights* w[3] = 
          {makeEntWeights(mesh, wtag, s, 0), 
           makeEntWeights(mesh, wtag, s, 1),
           makeEntWeights(mesh, wtag, s, mesh->getDimension())};
        Targets* t = makeVtxEdgeElmTargets(s, w, factor);
        Centroids c(mesh, wtag, s);
        Selector* sel = makeCentroidSelector(mesh, wtag, &c);
        parma::Balancer b(mesh, wtag, factor, s, w[2], t, sel); 
        return b.run(tolerance);
      }
      virtual void balance(apf::MeshTag* wtag, double tolerance) {
        int step=0;
        double t0 = MPI_Wtime();
        while (runVtxStep(wtag,tolerance) && step++ < 50);
        double t1 = MPI_Wtime();
        if (!PCU_Comm_Self())
          printf("vertices balanced to %f in %f seconds\n", tolerance, t1-t0);

        step=0;
        t0 = MPI_Wtime();
        while (runEdgeStep(wtag,tolerance) && step++ < 50);
        t1 = MPI_Wtime();
        if (!PCU_Comm_Self())
          printf("edges balanced to %f in %f seconds\n", tolerance, t1-t0);

        step=0;
        t0 = MPI_Wtime();
        while (runElmStep(wtag,tolerance) && step++ < 50);
        t1 = MPI_Wtime();
        if (!PCU_Comm_Self())
          printf("elements balanced to %f in %f seconds\n", tolerance, t1-t0);
      }
    private:
      apf::Mesh* mesh;
      double factor;
      int verbose;
  };
} //end parma namespace

apf::Balancer* Parma_MakeVtxEdgeElmBalancer(apf::Mesh* m, 
    double stepFactor, int verbosity) {
  return new parma::VtxEdgeElmBalancer(m, stepFactor, verbosity);
}
