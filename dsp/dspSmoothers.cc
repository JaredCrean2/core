#include "dspSmoothers.h"
#include <math.h>
#include <apf.h>
#include <apfMesh2.h>
#include <apfNumbering.h>
#include <cmath>
#include <vector>
#include <queue>

using namespace std;

namespace dsp {
  
  Smoother::~Smoother()
  {
  }
  
  class LaplacianSmoother : public Smoother {
  public:
    void smooth(apf::Field* df, Boundary& fixed, Boundary& moving)
    {
      apf::Mesh* m = apf::getMesh(df);
      /* start Fan's code */
      apf::MeshIterator* it;
      apf::MeshEntity* v;
      apf::ModelEntity* me;
      apf::Vector3 d;
      
      //---------------------------------------------------------
      int n_mb = 0; int n_in = 0; int n_fb = 0;
      
      //iterate vertex to count the number of each type of vertex
      it = m->begin(0);
      while ((v = m->iterate(it))) {
        me = m->toModel(v);
        if (moving.count(me)) n_mb++;
        else if (fixed.count(me)) n_fb++;
        else n_in++;
      }
      m->end(it);
      
      //----------------------------------------------------------
      int MB_begin = 0;            int MB_end = n_mb - 1;
      int IN_begin = n_mb;         int IN_end = n_mb + n_in - 1;
      int FB_begin = n_mb + n_in;  //int FB_end = n_mb + n_in + n_fb - 1;
      int ithMB = 0; int ithIN =0; int ithFB = 0;
      
      vector < apf::MeshEntity* > V_total(n_mb + n_in + n_fb);
      vector < apf::Vector3 > D_total(n_mb + n_in + n_fb);
      apf::Numbering* numbers = numberOwnedDimension(m, "my_numbering", 0);
      
      //iterate to store vertices and points
      it = m->begin(0);
      while ((v = m->iterate(it))) {
        apf::getVector(df, v, 0, d);
        me = m->toModel(v);
        if (moving.count(me)) {
          V_total[MB_begin + ithMB] = v;
          D_total[MB_begin + ithMB] = d;
          apf::number(numbers, v, 0, 0, MB_begin + ithMB);
          ithMB++;
        }
        else if (fixed.count(me)) {
          V_total[FB_begin + ithFB] = v;
          D_total[FB_begin + ithFB] = d;
          apf::number(numbers, v, 0, 0, FB_begin + ithFB);
          ithFB++;
        }
        else {
          V_total[IN_begin + ithIN] = v;
          D_total[IN_begin + ithIN] = d;
          // apf::number(numbers, v, 0, 0, IN_begin + ithIN);
          ithIN++;
        }
      }
      m->end(it);
      
      //----------------------------------------------------------
      ithIN = 0; int zero = 0; int one = 1;
      apf::Adjacent adj;
      
      //make a queue and put all MB vertex in it
      queue < apf::MeshEntity* > q;
      for (int i = MB_begin ; i < MB_end + 1 ; i++) {
        q.push(V_total[i]);
      }
      
      //tag = 1, indicates this is in queue before AND this is a interior vertex
      apf::MeshTag* in_queue_tag = m->createIntTag("In_Queue_Tag", 1);
      it = m->begin(0);
      while ((v = m->iterate(it))) {
        m->setIntTag(v, in_queue_tag, &zero);
      }
      m->end(it);
      
      //find its adj, if it is never in queue, put it in queue
      while (!q.empty()) {
        v = q.front();
        me = m->toModel(v);
        apf::getVector(df, v, 0, d);
        m->getAdjacent(v, 1, adj);
        int num_adj = adj.getSize();
        for (int i = 0 ; i < num_adj ; i++) {
          apf::MeshEntity* adj_v = apf::getEdgeVertOppositeVert(m, adj[i], v);
          apf::ModelEntity* adj_v_me = m->toModel(adj_v);
          if ((!moving.count(adj_v_me)) & (!fixed.count(adj_v_me))) {
            int tag;
            m->getIntTag(adj_v, in_queue_tag, &tag);
            if (tag == 0) {
              q.push(adj_v);
              m->setIntTag(adj_v, in_queue_tag, &one);
            }
          }
        }
        if ((!moving.count(me)) & (!fixed.count(me))) {
          V_total[IN_begin + ithIN] = v;
          D_total[IN_begin + ithIN] = d;
          apf::number(numbers, v, 0, 0, IN_begin + ithIN);
          ithIN++;
        }
        q.pop();
      }
      
      //----------------------------------------------------------
      double tol = 1.0E-5; //tolerance
      apf::Vector3 D_temp = apf::Vector3(0, 0, 0);
      
      // average nodal position = sum(all adj_V's position)/num of adj_V
      // check max, stop until it is less the tolerance
      double max = 1.0;
      while (max > tol) {
        max = 0.0;
        for (int i = IN_begin ; i < IN_end + 1 ; i++) {
          m->getAdjacent(V_total[i], 1, adj);
          int num_adj = adj.getSize();
          for (int j = 0 ; j < num_adj ; j++) {
            apf::MeshEntity* adj_v = apf::getEdgeVertOppositeVert(m, adj[j], V_total[i]);
            int adj_v_id = apf::getNumber(numbers, adj_v, 0, 0);
            D_temp = D_temp + D_total[adj_v_id];
          }
          D_temp[0] = D_temp[0]/num_adj;
          D_temp[1] = D_temp[1]/num_adj;
          D_temp[2] = D_temp[2]/num_adj;
          D_total[i] = D_temp;
          apf::setVector(df, V_total[i], 0, D_total[i]);
          
          double temp_max = sqrt(pow(D_total[i][0], 2) + pow(D_total[i][1], 2) + pow(D_total[i][2], 2));
          if (max < temp_max) {
            max = temp_max;
          }
        }
      }
      
      /* end Fan's code */
      (void)m;
      (void)df;
      (void)fixed;
      (void)moving;
    }
  };

  class SemiSpringSmoother : public Smoother {
  public:
    void smooth(apf::Field* df, Boundary& fixed, Boundary& moving)
    {
      apf::Mesh* m = apf::getMesh(df);
      /* start Fan's code */
      apf::MeshIterator* it;
      apf::MeshEntity* v;
      apf::ModelEntity* me;
      apf::Vector3 d;
      
      //---------------------------------------------------------
      int n_mb = 0; int n_in = 0; int n_fb = 0;
      
      //iterate vertex to count the number of each type of vertex
      it = m->begin(0);
      while ((v = m->iterate(it))) {
        me = m->toModel(v);
        if (moving.count(me)) n_mb++;
        else if (fixed.count(me)) n_fb++;
        else n_in++;
      }
      m->end(it);
      
      //----------------------------------------------------------
      int MB_begin = 0;            int MB_end = n_mb - 1;
      int IN_begin = n_mb;         int IN_end = n_mb + n_in - 1;
      int FB_begin = n_mb + n_in;  //int FB_end = n_mb + n_in + n_fb - 1;
      int ithMB = 0; int ithIN =0; int ithFB = 0;
      
      vector < apf::MeshEntity* > V_total(n_mb + n_in + n_fb);
      vector < apf::Vector3 > D_total(n_mb + n_in + n_fb);
      apf::Numbering* numbers = numberOwnedDimension(m, "my_numbering", 0);
      
      //iterate to store vertices and points
      it = m->begin(0);
      while ((v = m->iterate(it))) {
        apf::getVector(df, v, 0, d);
        me = m->toModel(v);
        if (moving.count(me)) {
          V_total[MB_begin + ithMB] = v;
          D_total[MB_begin + ithMB] = d;
          apf::number(numbers, v, 0, 0, MB_begin + ithMB);
          ithMB++;
        }
        else if (fixed.count(me)) {
          V_total[FB_begin + ithFB] = v;
          D_total[FB_begin + ithFB] = d;
          apf::number(numbers, v, 0, 0, FB_begin + ithFB);
          ithFB++;
        }
        else {
          V_total[IN_begin + ithIN] = v;
          D_total[IN_begin + ithIN] = d;
          // apf::number(numbers, v, 0, 0, IN_begin + ithIN);
          ithIN++;
        }
      }
      m->end(it);
      
      //----------------------------------------------------------
      ithIN = 0; int zero = 0; int one = 1;
      apf::Adjacent adj;
      
      //make a queue and put all MB vertex in it
      queue < apf::MeshEntity* > q;
      for (int i = MB_begin ; i < MB_end + 1 ; i++) {
        q.push(V_total[i]);
      }
      
      //tag = 1, indicates this is in queue before AND this is a interior vertex
      apf::MeshTag* in_queue_tag = m->createIntTag("In_Queue_Tag", 1);
      it = m->begin(0);
      while ((v = m->iterate(it))) {
        m->setIntTag(v, in_queue_tag, &zero);
      }
      m->end(it);
      
      //find its adj, if it is never in queue, put it in queue
      while (!q.empty()) {
        v = q.front();
        me = m->toModel(v);
        apf::getVector(df, v, 0, d);
        m->getAdjacent(v, 1, adj);
        int num_adj = adj.getSize();
        for (int i = 0 ; i < num_adj ; i++) {
          apf::MeshEntity* adj_v = apf::getEdgeVertOppositeVert(m, adj[i], v);
          apf::ModelEntity* adj_v_me = m->toModel(adj_v);
          if ((!moving.count(adj_v_me)) & (!fixed.count(adj_v_me))) {
            int tag;
            m->getIntTag(adj_v, in_queue_tag, &tag);
            if (tag == 0) {
              q.push(adj_v);
              m->setIntTag(adj_v, in_queue_tag, &one);
            }
          }
        }
        if ((!moving.count(me)) & (!fixed.count(me))) {
          V_total[IN_begin + ithIN] = v;
          D_total[IN_begin + ithIN] = d;
          apf::number(numbers, v, 0, 0, IN_begin + ithIN);
          ithIN++;
        }
        q.pop();
      }
      
      //----------------------------------------------------------
      double tol = 1.0E-5; //tolerance
      vector < apf::Vector3 > delta_P(n_mb + n_in + n_fb);
      apf::Downward down;
      double stiffness_temp; double cos_squ; double length_squ;
      vector < apf::Vector3 > tet_P(3);
      
      for (int i = 0 ; i < n_mb + n_in + n_fb ; i++) {
        delta_P[i] = apf::Vector3(0, 0, 0);
      }
      
      // average nodal position = sum(all adj_V's position)/num of adj_V
      // check max, stop until it is less the tolerance
      double max = 1.0;
      while (max > tol) {
        max = 0.0;
        for (int i = IN_begin ; i < IN_end + 1 ; i++) {
          apf::Vector3 delta_sum = apf::Vector3(0, 0, 0);
          double stiffness_sum = 0.0;
          m->getAdjacent(V_total[i], 3, adj);
          int num_adj = adj.getSize();
          for (int j = 0 ; j < num_adj ; j++) {
            int num_down; //num_down is sopposed to be 4
            num_down = m->getDownward(adj[j], 0, down);
            int tet_id = 0;
            for (int k = 0 ; k < num_down ; k++)
              if (down[k] != V_total[i]) {
                m->getPoint(down[k], 0, tet_P[tet_id]);
                tet_id++;
              }
            //----------------------------------------------------
            apf::Vector3 n_1; apf::Vector3 n_2;
            // j = 0; k = 1; l = 2; i = V_total[i]
            n_1[0] = (tet_P[2][1] - tet_P[0][1]) * (tet_P[1][2] - tet_P[0][2])
            - (tet_P[2][2] - tet_P[0][2]) * (tet_P[1][1] - tet_P[0][1]);
            
            n_1[1] = (tet_P[2][2] - tet_P[0][2]) * (tet_P[1][0] - tet_P[0][0])
            - (tet_P[2][0] - tet_P[0][0]) * (tet_P[1][2] - tet_P[0][2]);
            
            n_1[2] = (tet_P[2][0] - tet_P[0][0]) * (tet_P[1][1] - tet_P[0][1])
            - (tet_P[2][1] - tet_P[0][1]) * (tet_P[1][0] - tet_P[0][0]);
            
            n_2[0] = (tet_P[2][1] - V_total[i][1]) * (tet_P[1][2] - V_total[i][2])
            - (tet_P[2][2] - V_total[i][2]) * (tet_P[1][1] - V_total[i][1]);
            
            n_2[1] = (tet_P[2][2] - V_total[i][2]) * (tet_P[1][0] - V_total[i][0])
            - (tet_P[2][0] - V_total[i][0]) * (tet_P[1][2] - V_total[i][2]);
            
            n_2[2] = (tet_P[2][0] - V_total[i][0]) * (tet_P[1][1] - V_total[i][1])
            - (tet_P[2][1] - V_total[i][1]) * (tet_P[1][0] - V_total[i][0]);
            
            cos_squ = pow(n_1[0] * n_2[0] + n_1[1] * n_2[1] + n_1[2] * n_2[2], 2)
            /(pow(n_1[0], 2) + pow(n_1[1], 2) + pow(n_1[2], 2))
            /(pow(n_2[0], 2) + pow(n_2[1], 2) + pow(n_2[2], 2));
            
            length_squ = pow(tet_P[0][0] - V_total[i][0], 2)
            + pow(tet_P[0][1] - V_total[i][1], 2)
            + pow(tet_P[0][2] - V_total[i][2], 2);
            
            stiffness_sum = stiffness_sum + 1/(1 - cos_squ) + 1/8/length_squ;
            
            // l = 0; j = 1; k = 2; i = V_total[i]
            n_1[0] = (tet_P[0][1] - tet_P[1][1]) * (tet_P[2][2] - tet_P[1][2])
            - (tet_P[0][2] - tet_P[1][2]) * (tet_P[2][1] - tet_P[1][1]);
            
            n_1[1] = (tet_P[0][2] - tet_P[1][2]) * (tet_P[2][0] - tet_P[1][0])
            - (tet_P[0][0] - tet_P[1][0]) * (tet_P[2][2] - tet_P[1][2]);
            
            n_1[2] = (tet_P[0][0] - tet_P[1][0]) * (tet_P[2][1] - tet_P[1][1])
            - (tet_P[0][1] - tet_P[1][1]) * (tet_P[2][0] - tet_P[1][0]);
            
            n_2[0] = (tet_P[0][1] - V_total[i][1]) * (tet_P[2][2] - V_total[i][2])
            - (tet_P[0][2] - V_total[i][2]) * (tet_P[2][1] - V_total[i][1]);
            
            n_2[1] = (tet_P[0][2] - V_total[i][2]) * (tet_P[2][0] - V_total[i][0])
            - (tet_P[0][0] - V_total[i][0]) * (tet_P[2][2] - V_total[i][2]);
            
            n_2[2] = (tet_P[0][0] - V_total[i][0]) * (tet_P[2][1] - V_total[i][1])
            - (tet_P[0][1] - V_total[i][1]) * (tet_P[2][0] - V_total[i][0]);
            
            cos_squ = pow(n_1[0] * n_2[0] + n_1[1] * n_2[1] + n_1[2] * n_2[2], 2)
            /(pow(n_1[0], 2) + pow(n_1[1], 2) + pow(n_1[2], 2))
            /(pow(n_2[0], 2) + pow(n_2[1], 2) + pow(n_2[2], 2));
            
            length_squ = pow(tet_P[1][0] - V_total[i][0], 2)
            + pow(tet_P[1][1] - V_total[i][1], 2)
            + pow(tet_P[1][2] - V_total[i][2], 2);
            
            stiffness_sum = stiffness_sum + 1 / (1 - cos_squ) + 1/8/length_squ;
            
            // k = 0; l = 1; j = 2; i = V_total[i]
            n_1[0] = (tet_P[1][1] - tet_P[2][1]) * (tet_P[0][2] - tet_P[2][2])
            - (tet_P[1][2] - tet_P[2][2]) * (tet_P[0][1] - tet_P[2][1]);
            
            n_1[1] = (tet_P[1][2] - tet_P[2][2]) * (tet_P[0][0] - tet_P[2][0])
            - (tet_P[1][0] - tet_P[2][0]) * (tet_P[0][2] - tet_P[2][2]);
            
            n_1[2] = (tet_P[1][0] - tet_P[2][0]) * (tet_P[0][1] - tet_P[2][1])
            - (tet_P[1][1] - tet_P[2][1]) * (tet_P[0][0] - tet_P[2][0]);
            
            n_2[0] = (tet_P[1][1] - V_total[i][1]) * (tet_P[0][2] - V_total[i][2])
            - (tet_P[1][2] - V_total[i][2]) * (tet_P[0][1] - V_total[i][1]);
            
            n_2[1] = (tet_P[1][2] - V_total[i][2]) * (tet_P[0][0] - V_total[i][0])
            - (tet_P[1][0] - V_total[i][0]) * (tet_P[0][2] - V_total[i][2]);
            
            n_2[2] = (tet_P[1][0] - V_total[i][0]) * (tet_P[0][1] - V_total[i][1])
            - (tet_P[1][1] - V_total[i][1]) * (tet_P[0][0] - V_total[i][0]);
            
            cos_squ = pow(n_1[0] * n_2[0] + n_1[1] * n_2[1] + n_1[2] * n_2[2], 2)
            /(pow(n_1[0], 2) + pow(n_1[1], 2) + pow(n_1[2], 2))
            /(pow(n_2[0], 2) + pow(n_2[1], 2) + pow(n_2[2], 2));
            
            length_squ = pow(tet_P[2][0] - V_total[i][0], 2)
            + pow(tet_P[2][1] - V_total[i][1], 2)
            + pow(tet_P[2][2] - V_total[i][2], 2);
            
            stiffness_sum = stiffness_sum + 1 / (1 - cos_squ) + 1/8/length_squ;
            //----------------------------------------------------
            
            
            
            
            apf::MeshEntity* adj_v = apf::getEdgeVertOppositeVert(m, adj[j], V_total[i]);
            int adj_v_id = apf::getNumber(numbers, adj_v, 0, 0);
            P_temp = P_temp + P_total[adj_v_id];
          }
          P_temp[0] = P_temp[0]/num_adj;
          P_temp[1] = P_temp[1]/num_adj;
          P_temp[2] = P_temp[2]/num_adj;
          delta_P[i] = P_temp - P_total[i];
          
          double temp_max = sqrt(pow(delta_P[i][0], 2) + pow(delta_P[i][1], 2) + pow(delta_P[i][2], 2));
          if (max < temp_max) {
            max = temp_max;
          }
          //update IN
          P_total[i] = P_total[i] + delta_P[i];
          apf::setVector(df, V_total[i], 0, P_total[i]);
        }
      }
      
      /* end Fan's code */
      (void)m;
      (void)df;
      (void)fixed;
      (void)moving;
    }
  };
  
  class EmptySmoother : public Smoother {
  public:
    void smooth(apf::Field* df, Boundary& fixed, Boundary& moving)
    {
      (void)df;
      (void)fixed;
      (void)moving;
    }
  };
  
  Smoother* Smoother::makeLaplacian()
  {
    return new LaplacianSmoother();
  }
  
  Smoother* Smoother::makeSemiSpring()
  {
    return new SemiSpringSmoother();
  }
  
  Smoother* Smoother::makeEmpty()
  {
    return new EmptySmoother();
  }
  
}
