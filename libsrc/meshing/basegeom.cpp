#include <set>

#include <mystdlib.h>
#include "meshing.hpp"
#include <core/register_archive.hpp>

namespace netgen
{
  struct PointTree
  {
      std::map<int, BoxTree<3>> tree;
      Box<3> bounding_box;

      PointTree( Box<3> bb ) : bounding_box(bb) {}

      void Insert(Point<3> p, PointIndex n, int index)
      {
        if(tree.count(index) == 0)
          tree.emplace(index, bounding_box);
        tree.at(index).Insert(p, p, n);
      }

      PointIndex Find(Point<3> p, int index) const
      {
          ArrayMem<int, 1> points;
          tree.at(index).GetIntersecting(p, p, points);
          if(points.Size()==0)
              throw Exception("cannot find mapped point " + ToString(p));
          return points[0];
      }
  };


  GeometryRegisterArray& GeometryRegister()
  {
    static GeometryRegisterArray geometryregister;
    return geometryregister;
  }

  GeometryRegister :: ~GeometryRegister()
  { ; }

  bool GeometryShape :: IsMappedShape( const GeometryShape & other_, const Transformation<3> & trafo, double tol ) const
  {
      throw Exception("GeometryShape::IsMappedShape not implemented for class " + Demangle(typeid(this).name()));
  }

  bool GeometryVertex :: IsMappedShape( const GeometryShape & other_, const Transformation<3> & trafo, double tol ) const
  {
      const auto other_ptr = dynamic_cast<const GeometryVertex*>(&other_);
      if(!other_ptr)
          return false;

      return Dist(trafo(GetPoint()), other_ptr->GetPoint()) < tol;
  }

  bool GeometryEdge :: IsMappedShape( const GeometryShape & other_, const Transformation<3> & trafo, double tol ) const
  {
      const auto other_ptr = dynamic_cast<const GeometryEdge*>(&other_);
      if(!other_ptr)
          return false;
      auto & e = *other_ptr;
      if (IsDegenerated(tol) || e.IsDegenerated(tol))
        return false;

      if(tol < Dist(trafo(GetCenter()), e.GetCenter()))
          return false;

      auto v0 = trafo(GetStartVertex().GetPoint());
      auto v1 = trafo(GetEndVertex().GetPoint());
      auto w0 = e.GetStartVertex().GetPoint();
      auto w1 = e.GetEndVertex().GetPoint();

      // have two closed edges, use midpoints to compare
      if(Dist(v0,v1) < tol && Dist(w0,w1) < tol)
      {
          v1 = trafo(GetPoint(0.5));
          w1 = other_ptr->GetPoint(0.5);
      }

      return( (Dist(v0, w0) < tol && Dist(v1, w1) < tol) ||
              (Dist(v0, w1) < tol && Dist(v1, w0) < tol) );
  }

  bool GeometryFace :: IsMappedShape( const GeometryShape & other_, const Transformation<3> & trafo, double tol ) const
  {
      const auto other_ptr = dynamic_cast<const GeometryFace*>(&other_);
      if(!other_ptr)
          return false;
      auto & f = *other_ptr;

      if(tol < Dist(GetCenter(), f.GetCenter()))
          return false;

      // simple check: check if there is a bijective mapping of mapped edges
      auto & other_edges = f.edges;
      if(edges.Size() != other_edges.Size())
          return false;

      auto nedges = edges.Size();
      Array<bool> is_mapped(nedges);
      is_mapped = false;

      for(auto e : edges)
      {
          int found_mapping = 0;
          for(auto other_e : other_edges)
              if(e->IsMappedShape(*other_e, trafo, tol))
                  found_mapping++;
          if(found_mapping != 1)
              return false;
      }

      return true;
  }

  bool GeometryFace :: IsConnectingCloseSurfaces() const
  {
    std::map<const GeometryShape*, bool> verts;
    for(const auto& edge : edges)
      {
        verts[&edge->GetStartVertex()] = false;
        verts[&edge->GetEndVertex()] = false;
      }
    for(const auto& [v, is_mapped] : verts)
      {
        if(is_mapped)
          continue;
        for(const auto& v_ident : v->identifications)
          {
            const auto& other = v_ident.to == v ? v_ident.from : v_ident.to;
            if(v_ident.type == Identifications::CLOSESURFACES &&
               verts.count(other))
              {
                verts[v] = true;
                verts[other] = true;
              }
          }
      }
    for(auto& [v, is_mapped] : verts)
      if(!is_mapped)
        return false;
    return true;
  }

  void GeometryFace :: RestrictHTrig(Mesh& mesh,
                                     const PointGeomInfo& gi0,
                                     const PointGeomInfo& gi1,
                                     const PointGeomInfo& gi2,
                                     const MeshingParameters& mparam,
                                     int depth, double h) const
  {
    auto p0 = GetPoint(gi0);
    auto p1 = GetPoint(gi1);
    auto p2 = GetPoint(gi2);
    auto longest = (p0-p1).Length();
    int cutedge = 2;
    if(auto len = (p0-p2).Length(); len > longest)
      {
        longest = len;
        cutedge = 1;
      }
    if(auto len = (p1-p2).Length(); len > longest)
      {
        longest = len;
        cutedge = 0;
      }
    PointGeomInfo gi_mid;
    gi_mid.u = (gi0.u + gi1.u + gi2.u)/3;
    gi_mid.v = (gi0.v + gi1.v + gi2.v)/3;

    if(depth % 3 == 0)
      {
        double curvature = 0.;
        curvature = max({curvature, GetCurvature(gi_mid),
                         GetCurvature(gi0), GetCurvature(gi1),
                         GetCurvature(gi2)});
        if(curvature < 1e-3)
          return;
        double kappa = curvature * mparam.curvaturesafety;
        h = mparam.maxh * kappa < 1 ? mparam.maxh : 1./kappa;
        if(h < 1e-4 * longest)
          return;
      }

    if(h < longest && depth < 10)
      {
        if(cutedge == 0)
          {
            PointGeomInfo gi_m;
            gi_m.u = 0.5 * (gi1.u + gi2.u);
            gi_m.v = 0.5 * (gi1.v + gi2.v);
            RestrictHTrig(mesh, gi_m, gi2, gi0, mparam, depth+1, h);
            RestrictHTrig(mesh, gi_m, gi0, gi1, mparam, depth+1, h);
          }
        else if(cutedge == 1)
          {
            PointGeomInfo gi_m;
            gi_m.u = 0.5 * (gi0.u + gi2.u);
            gi_m.v = 0.5 * (gi0.v + gi2.v);
            RestrictHTrig(mesh, gi_m, gi1, gi2, mparam, depth+1, h);
            RestrictHTrig(mesh, gi_m, gi0, gi1, mparam, depth+1, h);
          }
        else if(cutedge == 2)
          {
            PointGeomInfo gi_m;
            gi_m.u = 0.5 * (gi0.u + gi1.u);
            gi_m.v = 0.5 * (gi0.v + gi1.v);
            RestrictHTrig(mesh, gi_m, gi1, gi2, mparam, depth+1, h);
            RestrictHTrig(mesh, gi_m, gi2, gi0, mparam, depth+1, h);
          }
      }
    else
      {
        auto pmid = GetPoint(gi_mid);
        for(const auto& p : {p0, p1, p2, pmid})
          mesh.RestrictLocalH(p, h);
      }
  }

  namespace {
    struct Line
    {
      Point<3> p0, p1;
      inline double Length() const { return (p1-p0).Length(); }
      inline double Dist(const Line& other) const
      {
        Vec<3> n = p1-p0;
        Vec<3> q = other.p1-other.p0;
        double nq = n*q;
        Point<3> p = p0 + 0.5*n;
        double lambda = (p-other.p0)*n / (nq + 1e-10);
        if (lambda >= 0 && lambda <= 1)
          return (p-other.p0-lambda*q).Length();
        return 1e99;
      }
    };
  }

  void NetgenGeometry :: Clear()
  {
      vertices.SetSize0();
      edges.SetSize0();
      faces.SetSize0();
      solids.SetSize0();
  }

  void NetgenGeometry :: ProcessIdentifications()
  {
      for(auto i : Range(vertices))
          vertices[i]->nr = i;
      for(auto i : Range(edges))
          edges[i]->nr = i;
      for(auto i : Range(faces))
          faces[i]->nr = i;
      for(auto i : Range(solids))
          solids[i]->nr = i;

      auto mirror_identifications = [&] ( auto & shapes )
      {
          for(auto i : Range(shapes))
          {
              auto &s = shapes[i];
              s->nr = i;
              for(auto & ident : s->identifications)
                if(s.get() == ident.from && s.get() != ident.to)
                  ident.to->identifications.Append(ident);
          }
      };

      auto tol = 1e-8 * bounding_box.Diam();
      for(auto & f : faces)
        for(auto & ident: f->identifications)
          for(auto e : static_cast<GeometryFace*>(ident.from)->edges)
            for(auto e_other : static_cast<GeometryFace*>(ident.to)->edges)
              if(ident.trafo && e->IsMappedShape(*e_other, *ident.trafo, tol))
                e->identifications.Append( {e, e_other, ident.trafo, ident.type, ident.name} );

      for(auto & e : edges)
        for(auto & ident: e->identifications)
          {
              auto & from = static_cast<GeometryEdge&>(*ident.from);
              auto & to = static_cast<GeometryEdge&>(*ident.to);

              GeometryVertex * pfrom[] = { &from.GetStartVertex(), &from.GetEndVertex() };
              GeometryVertex * pto[] = { &to.GetStartVertex(), &to.GetEndVertex() };

              if(!ident.trafo) continue;

              // swap points of other edge if necessary
              Point<3> p_from0 = (*ident.trafo)(from.GetStartVertex().GetPoint());
              Point<3> p_from1 = (*ident.trafo)(from.GetEndVertex().GetPoint());
              Point<3> p_to0 = to.GetStartVertex().GetPoint();

              if(Dist(p_from1, p_to0) < Dist(p_from0, p_to0))
                  swap(pto[0], pto[1]);

              for(auto i : Range(2))
                  pfrom[i]->identifications.Append( {pfrom[i], pto[i], ident.trafo, ident.type, ident.name} );
          }

      mirror_identifications(vertices);
      mirror_identifications(edges);
      mirror_identifications(faces);


      auto find_primary = [&] (auto & shapes)
      {
          for(auto &s : shapes)
              s->primary = s.get();

          bool changed = true;

          while(changed) {
            changed = false;
            for(auto &s : shapes)
            {
              for(auto & ident : s->identifications)
              {
                  bool need_inverse = ident.from == s.get();
                  auto other = need_inverse ? ident.to : ident.from;
                  if(other->primary->nr < s->primary->nr)
                  {
                      s->primary = other->primary;
                      if(ident.trafo)
                      {
                        auto trafo = *ident.trafo;
                        if(need_inverse)
                            trafo = trafo.CalcInverse();
                        if(!s->primary_to_me)
                          s->primary_to_me = Transformation<3>( Vec<3>{0., 0., 0.} );
                        if(!other->primary_to_me)
                          other->primary_to_me = Transformation<3>( Vec<3>{0., 0., 0.} );
                        s->primary_to_me->Combine(trafo, *other->primary_to_me);
                        changed = true;
                    }
                  }
              }
            }
          }
      };

      find_primary(vertices);
      find_primary(edges);
      find_primary(faces);
  }

  void NetgenGeometry :: Analyse(Mesh& mesh,
                                 const MeshingParameters& mparam) const
  {
    static Timer t1("SetLocalMeshsize"); RegionTimer regt(t1);
    mesh.SetGlobalH(mparam.maxh);
    mesh.SetMinimalH(mparam.minh);

    mesh.SetLocalH(bounding_box.PMin(), bounding_box.PMax(),
                   mparam.grading);

    // only set meshsize for edges longer than this
    double mincurvelength = 1e-3 * bounding_box.Diam();

    if(mparam.uselocalh)
      {
        double eps = 1e-10 * bounding_box.Diam();
        const char* savetask = multithread.task;
        multithread.task = "Analyse Edges";

        // restrict meshsize on edges
        for(auto i : Range(edges))
          {
            multithread.percent = 100. * i/edges.Size();
            const auto & edge = edges[i];
            auto length = edge->GetLength();
            // skip very short edges
            if(length < mincurvelength)
              continue;
            static constexpr int npts = 20;
            // restrict mesh size based on edge length
            for(auto i : Range(npts+1))
              mesh.RestrictLocalH(edge->GetPoint(double(i)/npts), length/mparam.segmentsperedge);

            // restrict mesh size based on edge curvature
            double t = 0.;
            auto p_old = edge->GetPoint(t);
            while(t < 1.-eps)
              {
                t += edge->CalcStep(t, 1./mparam.curvaturesafety);
                if(t < 1.)
                  {
                    auto p = edge->GetPoint(t);
                    auto dist = (p-p_old).Length();
                    mesh.RestrictLocalH(p, dist);
                    p_old = p;
                  }
              }
          }

        multithread.task = "Analyse Faces";
        // restrict meshsize on faces
        for(auto i : Range(faces))
          {
            multithread.percent = 100. * i/faces.Size();
            const auto& face = faces[i];
            face->RestrictH(mesh, mparam);
          }

        if(mparam.closeedgefac.has_value())
          {
            multithread.task = "Analyse close edges";
            constexpr int sections = 100;
            Array<Line> lines;
            lines.SetAllocSize(sections*edges.Size());
            BoxTree<3> searchtree(bounding_box.PMin(),
                                  bounding_box.PMax());
            for(const auto& edge : edges)
              {
                if(edge->GetLength() < eps)
                  continue;
                double t = 0.;
                auto p_old = edge->GetPoint(t);
                auto t_old = edge->GetTangent(t);
                t_old.Normalize();
                for(auto i : IntRange(1, sections+1))
                  {
                    t = double(i)/sections;
                    auto p_new = edge->GetPoint(t);
                    auto t_new = edge->GetTangent(t);
                    t_new.Normalize();
                    auto cosalpha = fabs(t_old * t_new);
                    if((i == sections) || (cosalpha < cos(10./180 * M_PI)))
                      {
                        auto index = lines.Append({p_old, p_new});
                        searchtree.Insert(p_old, p_new, index);
                        p_old = p_new;
                        t_old = t_new;
                      }
                  }
              }
            Array<int> linenums;
            for(auto i : Range(lines))
              {
                const auto& line = lines[i];
                if(line.Length() < eps) continue;
                multithread.percent = 100.*i/lines.Size();
                Box<3> box;
                box.Set(line.p0);
                box.Add(line.p1);
                // box.Increase(max2(mesh.GetH(line.p0), mesh.GetH(line.p1)));
                box.Increase(line.Length());
                double mindist = 1e99;
                linenums.SetSize0();
                searchtree.GetIntersecting(box.PMin(), box.PMax(),
                                           linenums);
                for(auto num : linenums)
                  {
                    if(i == num) continue;
                    const auto & other = lines[num];
                    if((line.p0 - other.p0).Length2() < eps ||
                       (line.p0 - other.p1).Length2() < eps ||
                       (line.p1 - other.p0).Length2() < eps ||
                       (line.p1 - other.p1).Length2() < eps)
                      continue;
                    mindist = min2(mindist, line.Dist(other));
                  }
                if(mindist == 1e99) continue;
                mindist /= *mparam.closeedgefac + 1e-10;
                if(mindist < 1e-3 * bounding_box.Diam())
                  {
                    (*testout) << "extremely small local h: " << mindist
                               << " --> setting to " << 1e-3 * bounding_box.Diam() << endl;
                    (*testout) << "somewhere near " << line.p0 << " - " << line.p1 << endl
;
                    mindist = 1e-3 * bounding_box.Diam();
                  }
                mesh.RestrictLocalHLine(line.p0, line.p1, mindist);
              }
          }
        multithread.task = savetask;
      }

    for(const auto& mspnt : mparam.meshsize_points)
      mesh.RestrictLocalH(mspnt.pnt, mspnt.h, mspnt.layer);

    mesh.LoadLocalMeshSize(mparam.meshsizefilename);
  }

  void GeometryEdge :: Divide(const MeshingParameters & mparam, const Mesh & mesh, Array<Point<3>> & points, Array<double> & params)
  {
    static Timer tdivedgesections("Divide edge sections");
    static Timer tdivide("Divide Edges");
    RegionTimer rt(tdivide);
    // -------------------- DivideEdge -----------------
    if(properties.partition)
      {
        points.SetSize(properties.partition->Size());
        params.SetSize(properties.partition->Size()+2);
        params[0] = 0.0;
        params.Last() = 1.0;
        for(auto i : Range(properties.partition->Size()))
          {
            params[i+1] = (*properties.partition)[i];
            points[i] = GetPoint(params[i+1]);
          }
        return;
      }

    tdivedgesections.Start();
    auto layer = properties.layer;
    double safety = 0.5*(1.-mparam.grading);

    double lam = 0.0;
    Point<3> p = GetPoint(0.0);
    auto old_p = p;
    Array<double> hvalue, fine_params;
    hvalue.Append(.0);

    while (lam<1. && hvalue.Size() < 20000) {
      fine_params.Append(lam);
      auto h = mesh.GetH(old_p, layer);
      auto step = safety * h/GetTangent(lam).Length();
      lam += step;
      lam = min2(lam, 1.0);
      p = GetPoint(lam);
      hvalue.Append((hvalue.Size()==0 ? 0.0 : hvalue.Last()) + 1./h * (p-old_p).Length());
      old_p = p;
    }

    fine_params.Append(1.0);

    if(hvalue.Size()==20000 && lam<1.0)
      cout << "Warning: Could not divide Edge" << endl;

    tdivedgesections.Stop();

    // auto n = hvalue.Size()-1;
    int nsubedges = max2(1, int(floor(hvalue.Last()+0.5)));
    points.SetSize(nsubedges-1);
    params.SetSize(nsubedges+1);

    int i1 = 0;
    for(auto i : Range(1,nsubedges))
    {
      auto h_target = i*hvalue.Last()/nsubedges;
      while(hvalue[i1]<h_target && i1<hvalue.Size())
        i1++;

      if(i1==hvalue.Size())
      {
        points.SetSize(i-1);
        params.SetSize(i+1);
        cout << "divide edge: local h too small" << endl;
        break;
      }

      // interpolate lam between points
      auto lam0 = fine_params[i1-1];
      auto lam1 = fine_params[i1];
      auto h0 = hvalue[i1-1];
      auto h1 = hvalue[i1];

      auto fac = (h_target-h0)/(h1-h0);
      auto lam = lam0 + fac*(lam1-lam0);
      params[i] = lam;
      points[i-1] = MeshPoint(GetPoint(params[i]));
    }

    params[0] = 0.;
    params[nsubedges] = 1.;

    if(params[nsubedges] <= params[nsubedges-1])
    {
        cout << "CORRECTED" << endl;
        points.SetSize (nsubedges-2);
        params.SetSize (nsubedges);
        params[nsubedges-1] = 1.;
    }
  }

  void NetgenGeometry :: FindEdges(Mesh& mesh,
                                   const MeshingParameters& mparam) const
  {
    static Timer t1("MeshEdges"); RegionTimer regt(t1);
    const char* savetask = multithread.task;
    multithread.task = "Mesh Edges";

    PointTree tree( bounding_box );

    auto & identifications = mesh.GetIdentifications();

    Array<PointIndex> vert2meshpt(vertices.Size());
    vert2meshpt = PointIndex::INVALID;

    for(auto & vert : vertices)
      {
        auto pi = mesh.AddPoint(vert->GetPoint(), vert->properties.layer);
        vert2meshpt[vert->nr] = pi;
        mesh[pi].Singularity(vert->properties.hpref);
        mesh[pi].SetType(FIXEDPOINT);
        
        Element0d el(pi, pi-IndexBASE<PointIndex>()+1);
        el.name = vert->properties.GetName();
        mesh.SetCD3Name(pi-IndexBASE<PointIndex>()+1, el.name);
        mesh.pointelements.Append (el);
      }

    for(auto & vert : vertices)
        for(auto & ident : vert->identifications)
            identifications.Add(vert2meshpt[ident.from->nr],
                                vert2meshpt[ident.to->nr],
                                ident.name,
                                ident.type);

    // size_t segnr = 0;
    auto nedges = edges.Size();
    Array<Array<PointIndex>> all_pnums(nedges);
    Array<Array<double>> all_params(nedges);

    for (auto edgenr : Range(edges))
    {
        auto edge = edges[edgenr].get();
        PointIndex startp, endp;
        // throws if points are not found
        startp = vert2meshpt[edge->GetStartVertex().nr];
        endp = vert2meshpt[edge->GetEndVertex().nr];

        // ignore collapsed edges
        if(edge->IsDegenerated())
          continue;

        // ----------- Add Points to mesh and create segments -----
        auto & pnums = all_pnums[edgenr];
        auto & params = all_params[edgenr];
        Array<Point<3>> edge_points;
        Array<double> edge_params;

        if(edge->primary == edge)
        {
            // check if start and end vertex are identified (if so, we only insert one segment and do z-refinement later)
            bool is_identified_edge = false;
            auto v0 = vertices[edge->GetStartVertex().nr].get();
            auto v1 = vertices[edge->GetEndVertex().nr].get();
            for(auto & ident : v0->identifications)
            {
                auto other = ident.from == v0 ? ident.to : ident.from;
                if(other->nr == v1->nr && ident.type == Identifications::CLOSESURFACES)
                {
                    is_identified_edge = true;
                    break;
                }
            }

            if(is_identified_edge)
            {
                params.SetSize(2);
                params[0] = 0.;
                params[1] = 1.;
            }
            else
            {
                edge->Divide(mparam, mesh, edge_points, params);
            }
        }
        else
        {
            auto nr_primary = edge->primary->nr;
            auto & pnums_primary = all_pnums[nr_primary];
            // auto & params_primary = all_params[nr_primary];
            auto trafo = edge->primary_to_me;

            auto np = pnums_primary.Size();
            edge_points.SetSize(np-2);
            edge_params.SetSize(np-2);
            for(auto i : Range(np-2))
            {
                edge_points[i] = mesh[pnums_primary[i+1]];
                if(trafo)
                  edge_points[i] = (*trafo)(edge_points[i]);
                EdgePointGeomInfo gi;
                edge->ProjectPoint(edge_points[i], &gi);
                edge_params[i] = gi.dist;
            }

            params.SetSize(edge_params.Size()+2);

            for(auto i : Range(edge_params))
                params[i+1] = edge_params[i];

            if(edge_params.Size()>1)
            {
              // Just projecting (code below) does not work for closed edges (startp == endp)
              // In this case, there are at least 2 inner points which we use to check edge orientation
              bool reversed = edge_params[1] < edge_params[0];
              if(reversed)
              {
                params[0] = 1.0;
                params.Last() = 0.0;
              }
              else
              {
                params.Last() = 1.0;
                params[0] = 0.0;
              }
            }
            else
            {
              for(size_t i : std::vector{0UL, pnums_primary.Size()-1})
              {
                auto p_mapped = mesh[pnums_primary[i]];
                if(trafo) p_mapped = (*trafo)(p_mapped);
                EdgePointGeomInfo gi;
                edge->ProjectPoint(p_mapped, &gi);
                params[i] = gi.dist;
              }
            }
        }

        pnums.SetSize(edge_points.Size() + 2);

        bool is_reversed = params.Last() < params[0];
        pnums[0] = is_reversed ? endp : startp;
        pnums.Last() = is_reversed ? startp : endp;


        for(auto i : Range(edge_points))
        {
            auto pi = mesh.AddPoint(edge_points[i], edge->properties.layer);
            if(edge->identifications.Size())
              tree.Insert(mesh[pi], pi, edge->nr);
            pnums[i+1] = pi;
        }

        for(auto i : Range(pnums.Size()-1))
        {
          // segnr++;
            Segment seg;
            seg[0] = pnums[i];
            seg[1] = pnums[i+1];
            seg.edgenr = edgenr+1;
            seg.si = edgenr+1;
            seg.epgeominfo[0].dist = params[i];
            seg.epgeominfo[1].dist = params[i+1];
            seg.epgeominfo[0].edgenr = edgenr;
            seg.epgeominfo[1].edgenr = edgenr;
            seg.singedge_left = edge->properties.hpref;
            seg.singedge_right = edge->properties.hpref;
            seg.domin = edge->domin+1;
            seg.domout = edge->domout+1;
            mesh.AddSegment(seg);
        }
        mesh.SetCD2Name(edgenr+1, edge->properties.GetName());
    }

    for (auto & edge : edges)
    {
        // identify points on edge
        for(auto & ident : edge->identifications)
          if(ident.from == edge.get())
          {
            auto & pnums = all_pnums[edge->nr];
            if(pnums.Size() < 2) continue; // degenerated edge
            // start and end vertex are already identified
            for(auto pi : pnums.Range(1, pnums.Size()-1))
            {
                Point<3> p_other = mesh[pi];
                if(ident.trafo)
                  p_other = (*ident.trafo)(mesh[pi]);
                else
                  static_cast<GeometryEdge*>(ident.to)->ProjectPoint(p_other, nullptr);
                auto pi_other = tree.Find(p_other, ident.to->nr);
                identifications.Add(pi, pi_other, ident.name, ident.type);
            }
          }
    }
    mesh.CalcSurfacesOfNode();
    multithread.task = savetask;
  }

  bool NetgenGeometry :: MeshFace(Mesh& mesh, const MeshingParameters& mparam,
                     int k, FlatArray<int, PointIndex> glob2loc) const
  {
    multithread.percent = 100. * k/faces.Size();
    const auto& face = *faces[k];
    auto bb = face.GetBoundingBox();
    bb.Increase(bb.Diam()/10);
    Meshing2 meshing(*this, mparam, bb);
    glob2loc = 0;
    int cntp = 0;

    auto segments = face.GetBoundary(mesh);
    for(auto& seg : segments)
      {
        for(auto j : Range(2))
          {
            auto pi = seg[j];
            if(glob2loc[pi] == 0)
              {
                meshing.AddPoint(mesh[pi], pi);
                cntp++;
                glob2loc[pi] = cntp;
              }
          }
      }
    for(const auto& vert : GetFaceVertices(face))
      {
        PointIndex pi = vert->nr + 1;
        if(glob2loc[pi] == 0)
          {
            auto gi = face.Project(mesh[pi]);
            MultiPointGeomInfo mgi;
            mgi.AddPointGeomInfo(gi);
            meshing.AddPoint(mesh[pi], pi, &mgi);
            cntp++;
            glob2loc[pi] = cntp;
          }
      }
    for(auto & seg : segments)
      {
        PointGeomInfo gi0, gi1;
        gi0.trignum = gi1.trignum = k+1;
        gi0.u = seg.epgeominfo[0].u;
        gi0.v = seg.epgeominfo[0].v;
        gi1.u = seg.epgeominfo[1].u;
        gi1.v = seg.epgeominfo[1].v;
        meshing.AddBoundaryElement(glob2loc[seg[0]],
                                   glob2loc[seg[1]],
                                   gi0, gi1);
      }

    // TODO Set max area 2* area of face

    auto noldsurfels = mesh.GetNSE();


    static Timer t("GenerateMesh"); RegionTimer reg(t);
    MESHING2_RESULT res = meshing.GenerateMesh(mesh, mparam, mparam.maxh, k+1, face.properties.layer);

    for(auto i : Range(noldsurfels, mesh.GetNSE()))
      {
        mesh.SurfaceElements()[i].SetIndex(k+1);
      }
    return res != MESHING2_OK;
  }

  void NetgenGeometry :: MeshSurface(Mesh& mesh,
                                     const MeshingParameters& mparam) const
  {
    static Timer t1("Surface Meshing"); RegionTimer regt(t1);
    const char* savetask = multithread.task;
    multithread.task = "Mesh Surface";
    mesh.ClearFaceDescriptors();

    size_t n_failed_faces = 0;
    Array<int, PointIndex> glob2loc(mesh.GetNP());
    for(auto k : Range(faces))
    {
        auto & face = *faces[k];
        FaceDescriptor fd(k+1, face.domin+1, face.domout+1, k+1);
        if(face.properties.col)
          fd.SetSurfColour(*face.properties.col);
        mesh.AddFaceDescriptor(fd);
        mesh.SetBCName(k, face.properties.GetName());
        if(face.primary == &face)
        {
            // check if this face connects two identified closesurfaces
            // auto & idents = mesh.GetIdentifications();
            std::set<int> relevant_edges;
            auto segments = face.GetBoundary(mesh);
            for(const auto &s : segments)
                relevant_edges.insert(s.edgenr-1);

            Array<bool, PointIndex> is_point_in_tree(mesh.Points().Size());
            is_point_in_tree = false;
            PointTree tree( bounding_box );
            for(const auto &s : segments)
                for(auto pi : s.PNums())
                    if(!is_point_in_tree[pi])
                    {
                        tree.Insert(mesh[pi], pi, -1);
                        is_point_in_tree[pi] = true;
                    }

            Array<int> mapped_edges(edges.Size());
            constexpr int UNINITIALIZED = -2;
            constexpr int NOT_MAPPED = -1;
            mapped_edges = UNINITIALIZED;

            optional<Transformation<3>> trafo;

            if(face.IsConnectingCloseSurfaces())
            {
                Array<ArrayMem<int, 2>, PointIndex> p2seg(mesh.Points().Size());
                for(int si : Range(segments))
                {
                    const auto & s = segments[si];
                    p2seg[s[0]].Append(si);
                    p2seg[s[1]].Append(si);
                }
                for(const auto & s : segments)
                {
                    auto edgenr = s.edgenr-1;
                    auto & edge = *edges[edgenr];
                    // ShapeIdentification *edge_mapping;

                    // have edgenr first time, search for closesurface identification

                    if(mapped_edges[edgenr] == UNINITIALIZED)
                    {
                        mapped_edges[edgenr] = NOT_MAPPED;
                        for(auto & edge_ident : edge.identifications)
                        {
                            if(edge_ident.type == Identifications::CLOSESURFACES &&
                                    edge_ident.from->nr == edgenr &&
                                    relevant_edges.count(edge_ident.to->nr) > 0
                              )
                            {
                                trafo = edge_ident.trafo;
                                mapped_edges[edgenr] = edge_ident.to->nr;
                                break;
                            }
                        }
                    }

                    // this edge has a closesurface mapping to another -> make connecting quad
                    if(mapped_edges[edgenr] != NOT_MAPPED)
                    {
                        Element2d sel(4);
                        sel[0] = s[0];
                        sel[1] = s[1];
                        auto gis = sel.GeomInfo();
                        for(auto i : Range(2))
                        {
                            gis[i].u = s.epgeominfo[i].u;
                            gis[i].v = s.epgeominfo[i].v;
                        }

                        Point<3> p2 = mesh[s[1]];
                        Point<3> p3 = mesh[s[0]];
                        if(trafo)
                        {
                          p2 = (*trafo)(p2);
                          p3 = (*trafo)(p3);
                        }
                        else 
                        {
                          edges[mapped_edges[edgenr]]->ProjectPoint(p2, nullptr);
                          edges[mapped_edges[edgenr]]->ProjectPoint(p3, nullptr);
                        }
                        sel[2] = tree.Find(p2, -1);
                        sel[3] = tree.Find(p3, -1);

                        // find mapped segment to set PointGeomInfo correctly
                        Segment s_other;
                        for(auto si_other : p2seg[sel[2]])
                        {
                            s_other = segments[si_other];
                            if(s_other[0] == sel[2] && s_other[1] == sel[3])
                                break;
                            if(s_other[0] == sel[3] && s_other[1] == sel[2])
                                break;
                        }
                        for(auto i : Range(2))
                        {
                            auto i_other = sel[i+2] == s_other[i] ? i : 1-i;
                            gis[i+2].u = s_other.epgeominfo[i_other].u;
                            gis[i+2].v = s_other.epgeominfo[i_other].v;
                        }

                        sel.SetIndex(face.nr+1);
                        mesh.AddSurfaceElement(sel);
                    }
                }
            }
            else
                if(MeshFace(mesh, mparam, k, glob2loc))
                    n_failed_faces++;
        }
    }

    if(n_failed_faces) 
    {
        cout << "WARNING! NOT ALL FACES HAVE BEEN MESHED" << endl;
        cout << "SURFACE MESHING ERROR OCCURRED IN " << n_failed_faces << " FACES:" << endl;
        return;
    }

    if (mparam.perfstepsend >= MESHCONST_OPTSURFACE)
    {
      mesh.CalcSurfacesOfNode();
      OptimizeSurface(mesh, mparam);
    }

    bool have_identifications = false;
    std::map<tuple<PointIndex, int>, PointIndex> mapto;
    for(auto & face : faces)
        if(face->primary != face.get())
        {
            have_identifications = true;
            MapSurfaceMesh(mesh, *face, mapto);
        }

    // identify points on faces
    if(have_identifications)
    {
        mesh.CalcSurfacesOfNode();
        BitArray is_identified_face(faces.Size());
        is_identified_face = false;
        for(auto & face : faces)
            for(auto & ident : face->identifications)
            {
                is_identified_face.SetBit(ident.from->nr);
                is_identified_face.SetBit(ident.to->nr);
            }

        PointTree tree( bounding_box );
        Array<int, PointIndex> pi_to_face(mesh.GetNP());
        pi_to_face = -1;
        Array<SurfaceElementIndex> si_of_face;
        Array<Array<PointIndex>> pi_of_face(faces.Size());
        for(auto & face : faces)
            if(is_identified_face[face->nr])
            {
                mesh.GetSurfaceElementsOfFace(face->nr+1, si_of_face);
                for(auto si : si_of_face)
                    for(auto pi : mesh[si].PNums())
                    {
                        if(mesh[pi].Type() == SURFACEPOINT && pi_to_face[pi]==-1)
                        {
                            pi_to_face[pi] = face->nr;
                            tree.Insert(mesh[pi], pi, -1);
                            pi_of_face[face->nr].Append(pi);
                        }
                    }
            }

        auto & mesh_ident = mesh.GetIdentifications();
        for(auto & face : faces)
            for(auto & ident : face->identifications)
            {
                if(ident.from == face.get())
                    for(auto pi : pi_of_face[face->nr])
                    {
                        auto pi_primary = ident.from->primary->nr == ident.from->nr ? pi : mapto[{pi, ident.to->primary->nr}];
                        auto pi_other = ident.to->primary->nr == ident.to->nr ? pi_primary : mapto[{pi_primary, ident.to->nr}];
                        mesh_ident.Add(pi, pi_other, ident.name, ident.type);
                    }
            }
    }

    mesh.CalcSurfacesOfNode();
    multithread.task = savetask;
  }

  void NetgenGeometry :: MapSurfaceMesh( Mesh & mesh, const GeometryFace & dst, std::map<tuple<PointIndex, int>, PointIndex> & mapto ) const
  {
    static Timer timer("MapSurfaceMesh");
    RegionTimer rt(timer);

    const auto & src = dynamic_cast<const GeometryFace&>(*dst.primary);
    auto trafo = dst.primary_to_me;

    PrintMessage(2, "Map face ", src.nr+1, " -> ", dst.nr+1);

    // point map from src to dst
    auto np = mesh.Points().Size();
    Array<PointIndex, PointIndex> pmap(np);
    pmap = PointIndex::INVALID;
    BitArray is_double_edge_point(np);
    is_double_edge_point.Clear();

    // first map points on edges (mapped points already in mesh, use search tree)
    Array<bool, PointIndex> is_point_in_tree(mesh.Points().Size());
    is_point_in_tree = false;
    PointTree tree( bounding_box );
    for (Segment & seg : src.GetBoundary(mesh))
        for(auto i : Range(2))
          {
            auto pi = seg[i];
            if(!is_point_in_tree[pi])
            {
              auto p = mesh[pi];
              if(trafo)
                p = (*trafo)(p);
              else
                for(auto& edge: dst.edges)
                  if (edge->primary->nr == seg.edgenr-1)
                    {
                      if (mesh[pi].Type() == FIXEDPOINT) {
                        if((edge->GetStartVertex().GetPoint() - p).Length2() >\
                           (edge->GetEndVertex().GetPoint() - p).Length2())
                          p = edge->GetEndVertex().GetPoint();
                        else
                          p = edge->GetStartVertex().GetPoint();
                      }
                      else
                        edge->ProjectPoint(p, nullptr);
                    }
              tree.Insert(p, pi, -1);
              is_point_in_tree[pi] = true;
            }
          }

    Array<ArrayMem<tuple<double,double>, 2>, PointIndex> uv_values(np);
    for (Segment & seg : dst.GetBoundary(mesh))
    {
        for(auto i : Range(2))
          {
            auto pi = seg[i];
            if(!pmap[pi].IsValid())
              pmap[tree.Find(mesh[pi], -1)] = pi;

            // store uv values (might be different values for same point in case of internal edges)
            double u = seg.epgeominfo[i].u;
            double v = seg.epgeominfo[i].v;
            auto & vals = uv_values[pi];
            bool found = false;
            for(const auto & [u1,v1] : vals)
                if((u-u1)*(u-u1)+(v-v1)*(v-v1) < 1e-7)
                    found = true;
            if(!found)
                vals.Append({u,v});
          }
    }

    xbool do_invert = maybe;
    if(!trafo) do_invert = true;

    // now insert mapped surface elements
    for(auto sei : mesh.SurfaceElements().Range())
      {
        auto sel = mesh[sei];
        if(sel.GetIndex() != src.nr+1)
          continue;

        auto sel_new = sel;
        sel_new.SetIndex(dst.nr+1);
        for(auto i : Range(sel.PNums()))
          {
            auto pi = sel[i];
            if(!pmap[pi].IsValid())
              {
                auto p = mesh[pi];
                if(trafo)
                  p = (*trafo)(p);
                else
                  dst.Project(p);
                pmap[pi] = mesh.AddPoint(p, 1, SURFACEPOINT);
              }
              sel_new[i] = pmap[pi];
              mapto[{pi, dst.nr}] = pmap[pi];
              mapto[{pmap[pi], src.nr}] = pi;
          }
        if(do_invert.IsMaybe())
        {
            auto n_src = src.GetNormal(mesh[sel[0]]);
            auto n_dist = dst.GetNormal(mesh[sel_new[0]]);
            Mat<3> normal_matrix;
            CalcInverse(Trans(trafo->GetMatrix()), normal_matrix);
            do_invert = (normal_matrix * n_src) * n_dist < 0.0;
        }
        if(do_invert.IsTrue())
            sel_new.Invert();

        for(auto i : Range(sel.PNums()))
        {
            auto pi = sel_new[i];
            if(uv_values.Range().Next() <= pi)
            {
                // new point (inner surface point)
                PointGeomInfo gi;
                dst.CalcPointGeomInfo(mesh[sel_new[i]], gi);
                sel_new.GeomInfo()[i] = gi;
                continue;
            }

            const auto & uvs = uv_values[pi];
            if(uvs.Size() == 1)
            {
                // appears only once -> take uv values from edgepointgeominfo
                const auto & [u,v] = uvs[0];
                PointGeomInfo gi;
                gi.u = u;
                gi.v = v;
                sel_new.GeomInfo()[i] = gi;
            }
            else if(uvs.Size() > 1)
            {
                // multiple uv pairs -> project to close point and select closest uv pair
                double eps = 1e-3;
                auto p = Point<3>((1.0-eps)*Vec<3>(mesh[sel_new.PNumMod(i+1)]) +
                                      eps/2*Vec<3>(mesh[sel_new.PNumMod(i+2)]) +
                                      eps/2*Vec<3>(mesh[sel_new.PNumMod(i+3)]));
                PointGeomInfo gi_p, gi;
                dst.CalcPointGeomInfo(p, gi_p);
                gi.trignum = gi_p.trignum;
                double min_dist = numeric_limits<double>::max();
                for(const auto & [u,v] : uvs)
                {
                    double dist = (gi_p.u-u)*(gi_p.u-u) + (gi_p.v-v)*(gi_p.v-v);
                    if(dist < min_dist)
                    {
                        min_dist = dist;
                        gi.u = u;
                        gi.v = v;
                    }
                }
                sel_new.GeomInfo()[i] = gi;
            }
            else
                throw Exception(string(__FILE__) + ":"+ToString(__LINE__) + " shouldn't come here");
          }
          mesh.AddSurfaceElement(sel_new);
      }
  }

  void NetgenGeometry :: OptimizeSurface(Mesh& mesh, const MeshingParameters& mparam) const
  {
    const auto savetask = multithread.task;
    multithread.task = "Optimizing surface";

    static Timer timer_opt2d("Optimization 2D");
    RegionTimer reg(timer_opt2d);
    auto meshopt = MeshOptimize2d(mesh);
    for(auto i : Range(mparam.optsteps2d))
    for(auto k : Range(mesh.GetNFD()))
      {
        PrintMessage(3, "Optimization step ", i);
        meshopt.SetFaceIndex(k+1);
        meshopt.SetMetricWeight (mparam.elsizeweight);
        int innerstep = 0;
        for(auto optstep : mparam.optimize2d)
          {
            multithread.percent = 100. * (double(innerstep++)/mparam.optimize2d.size() + i)/mparam.optsteps2d;
            switch(optstep)
              {
              case 's':
                meshopt.EdgeSwapping(0);
                break;
              case 'S':
                meshopt.EdgeSwapping(1);
                break;
              case 'm':
                meshopt.ImproveMesh(mparam);
                break;
              case 'c':
                meshopt.CombineImprove();
                break;
              }
          }
      }
    mesh.CalcSurfacesOfNode();
    mesh.Compress();
    multithread.task = savetask;
  }

  void NetgenGeometry :: FinalizeMesh(Mesh& mesh) const
  {
      for (int i = 0; i < std::min(solids.Size(), (size_t)mesh.GetNDomains()); i++)
        if (auto name = solids[i]->properties.name)
          mesh.SetMaterial (i+1, *name);

    mesh.OrderElements();
  }
  
  shared_ptr<NetgenGeometry> GeometryRegisterArray :: LoadFromMeshFile (istream & ist) const
  {
    if (!ist.good())
        return nullptr;

    string token;
    ist >> token;
    if(token == "TextOutArchive")
    {
        NetgenGeometry *geo = nullptr;
        size_t string_length;
        ist >> string_length;
        string buffer(string_length+1, '\0');
        ist.read(&buffer[0], string_length);
        auto ss = make_shared<stringstream>(buffer);
        TextInArchive in(ss);
        in & geo;

        return shared_ptr<NetgenGeometry>(geo);
    }
    for (int i = 0; i < Size(); i++)
      {
        NetgenGeometry * hgeom = (*this)[i]->LoadFromMeshFile (ist, token);
        if (hgeom)
          return shared_ptr<NetgenGeometry>(hgeom);
      }
    return nullptr;
  }



  
  int NetgenGeometry :: GenerateMesh (shared_ptr<Mesh> & mesh, MeshingParameters & mp)
  {
    multithread.percent = 0;

    // copy so that we don't change them outside
    MeshingParameters mparam = mp;
    if(restricted_h.Size())
      for(const auto& [pnt, maxh] : restricted_h)
        mparam.meshsize_points.Append({pnt, maxh});

    if(mparam.perfstepsstart <= MESHCONST_ANALYSE)
      {
        if(!mesh)
          mesh = make_shared<Mesh>();
        mesh->geomtype = GetGeomType();
        Analyse(*mesh, mparam);
      }

    if(multithread.terminate || mparam.perfstepsend <= MESHCONST_ANALYSE)
      return 0;

    if(mparam.perfstepsstart <= MESHCONST_MESHEDGES)
      FindEdges(*mesh, mparam);

    if(multithread.terminate || mparam.perfstepsend <= MESHCONST_MESHEDGES)
      return 0;

    if(dimension == 1)
      {
        FinalizeMesh(*mesh);
        mesh->SetDimension(1);
        return 0;
      }

    if (mparam.perfstepsstart <= MESHCONST_MESHSURFACE)
      {
        MeshSurface(*mesh, mparam);
      }

    if (multithread.terminate || mparam.perfstepsend <= MESHCONST_OPTSURFACE)
      return 0;

    if(dimension == 2)
    {
        FinalizeMesh(*mesh);
        mesh->SetDimension(2);
        return 0;
    }

    if(mparam.perfstepsstart <= MESHCONST_MESHVOLUME)
      {
        multithread.task = "Volume meshing";

        MESHING3_RESULT res = MeshVolume (mparam, *mesh);

        if (res != MESHING3_OK) return 1;
        if (multithread.terminate) return 0;

        MeshQuality3d (*mesh);
      }

    if (multithread.terminate || mparam.perfstepsend <= MESHCONST_MESHVOLUME)
      return 0;


    if (mparam.perfstepsstart <= MESHCONST_OPTVOLUME)
      {
	multithread.task = "Volume optimization";

	OptimizeVolume (mparam, *mesh);
	if (multithread.terminate) return 0;
      }
    FinalizeMesh(*mesh);
    return 0;
  }

  void NetgenGeometry :: Save (const filesystem::path & filename) const
  {
    throw NgException("Cannot save geometry - no geometry available");
  }

  static RegisterClassForArchive<NetgenGeometry> regnggeo;
}
