#include "house_detector.hpp"

#include "../indexer/classificator.hpp"

#include "../base/logging.hpp"

#include "../geometry/distance.hpp"
#include "../geometry/distance_on_sphere.hpp"

#include "../std/set.hpp"
#include "../std/bind.hpp"


namespace search
{

/// @todo Move prefixes, suffixes into separate file (autogenerated).

string affics1[] =
{
  "аллея", "бульвар", "набережная",
  "переулок", "площадь",  "проезд",
  "проспект", "шоссе", "тупик", "улица"
};

string affics2[] =
{
  "ал.", "бул.", "наб.", "пер.",
  "пл.", "пр.", "просп.", "ш.",
  "туп.", "ул."
};

void GetStreetName(strings::SimpleTokenizer iter, string & streetName)
{
  while (iter)
  {
    bool flag = true;
    for (size_t i = 0; i < ARRAY_SIZE(affics2); ++i)
    {
      if (*iter == affics2[i] || *iter == affics1[i])
      {
        flag = false;
        break;
      }
    }
    if (flag)
      streetName += *iter;
    ++iter;
  }
}

double const STREET_CONNECTION_LENGTH_M = 25.0;

void House::InitHouseNumberAndSuffix()
{
  /// @todo Need to store many numbers for one house and parse according to it.

  m_suffix = "";
  m_intNumber = 0;
  for (size_t i = 0; i < m_number.size(); ++i)
    if (m_number[i] < '0' || m_number[i] > '9')
    {
      strings::to_int(m_number.substr(0, i), m_intNumber);
      m_suffix = m_number.substr(i, m_number.size() - i);
      return;
    }

  strings::to_int(m_number, m_intNumber);
}

struct StreetCreator
{
  Street * street;
  StreetCreator(Street * st) : street(st) {}
  void operator () (CoordPointT const & point) const
  {
    street->m_points.push_back(m2::PointD(point.first, point.second));
  }
};

FeatureLoader::FeatureLoader(Index const * pIndex)
  : m_pIndex(pIndex), m_pGuard(0)
{
}

FeatureLoader::~FeatureLoader()
{
  Free();
}

void FeatureLoader::CreateLoader(size_t mwmID)
{
  if (m_pGuard == 0 || mwmID != m_pGuard->GetID())
  {
    delete m_pGuard;
    m_pGuard = new Index::FeaturesLoaderGuard(*m_pIndex, mwmID);
  }
}

void FeatureLoader::Load(FeatureID const & id, FeatureType & f)
{
  CreateLoader(id.m_mwm);
  m_pGuard->GetFeature(id.m_offset, f);
}

void FeatureLoader::Free()
{
  delete m_pGuard;
  m_pGuard = 0;
}

template <class ToDo>
void FeatureLoader::ForEachInRect(m2::RectD const & rect, ToDo toDo)
{
  m_pIndex->ForEachInRect(toDo, rect, scales::GetUpperScale());
}

m2::RectD Street::GetLimitRect(double offsetMeters) const
{
  m2::RectD rect;
  for (size_t i = 0; i < m_points.size(); ++i)
    rect.Add(MercatorBounds::RectByCenterXYAndSizeInMeters(m_points[i], offsetMeters));
  return rect;
}

void Street::SetName(string const & name)
{
  m_name = name;
  strings::SimpleTokenizer iter(name, " -");
  GetStreetName(iter, m_processedName);
  strings::MakeLowerCase(m_processedName);
}

namespace
{
bool LessDistance(HouseProjection const & p1, HouseProjection const & p2)
{
  return p1.m_streetDistance < p2.m_streetDistance;
}
}

void Street::SortHousesProjection()
{
  sort(m_houses.begin(), m_houses.end(), &LessDistance);
}

HouseDetector::HouseDetector(Index const * pIndex)
  : m_loader(pIndex), m_end2st(LessWithEpsilon(&m_epsMercator)), m_streetNum(0)
{
  // default value for conversions
  SetMetres2Mercator(360.0 / 40.0E06);
}

void HouseDetector::SetMetres2Mercator(double factor)
{
  m_epsMercator = factor * STREET_CONNECTION_LENGTH_M;
}

void HouseDetector::FillQueue(queue<Street *> & q, Street const * street, bool isBeg)
{
  pair<IterT, IterT> const & range =
      m_end2st.equal_range(isBeg ? street->m_points.front() : street->m_points.back());

  for (IterT it = range.first; it != range.second; ++it)
  {
    if (it->second->m_number == -1 && Street::IsSameStreets(street, it->second))
    {
      it->second->m_number = m_streetNum;
      q.push(it->second);
    }
  }
}

void HouseDetector::Bfs(Street * st)
{
  queue<Street *> q;
  q.push(st);
  st->m_number = m_streetNum;
  m_streets.push_back(vector<Street *>());

  while (!q.empty())
  {
    Street * street = q.front();
    m_streets.back().push_back(street);
    q.pop();
    FillQueue(q, street, true);
    FillQueue(q, street, false);
  }
}

int HouseDetector::LoadStreets(vector<FeatureID> & ids)
{
  int count = 0;
  for (size_t i = 0; i < ids.size(); ++i)
  {
    if (m_id2st.find(ids[i]) != m_id2st.end())
      continue;

    FeatureType f;
    m_loader.Load(ids[i], f);
    if (f.GetFeatureType() == feature::GEOM_LINE)
    {
      /// @todo Assume that default name always exist as primary compare key.
      string name;
      if (!f.GetName(0, name) || name.empty())
        continue;

      ++count;

      Street * st = new Street();
      st->SetName(name);
      f.ForEachPoint(StreetCreator(st), FeatureType::BEST_GEOMETRY);

      m_id2st[ids[i]] = st;
      m_end2st.insert(pair<m2::PointD, Street *> (st->m_points.front(), st));
      m_end2st.insert(pair<m2::PointD, Street *> (st->m_points.back(), st));
    }
    else
      ASSERT(false, ());
  }

  m_loader.Free();
  return count;
}

int HouseDetector::MergeStreets()
{
  for (IterT it = m_end2st.begin(); it != m_end2st.end(); ++it)
  {
    if (it->second->m_number == -1)
    {
      Street * st = it->second;
      ++m_streetNum;
      Bfs(st);
    }
  }
  return m_streetNum;
}

namespace
{

class HouseChecker
{
  uint32_t m_types[2];
public:
  HouseChecker()
  {
    Classificator const & c = classif();

    char const * arr0[] = { "building" };
    m_types[0] = c.GetTypeByPath(vector<string>(arr0, arr0 + 1));
    char const * arr1[] = { "building", "address" };
    m_types[1] = c.GetTypeByPath(vector<string>(arr1, arr1 + 2));
  }

  bool IsHouse(feature::TypesHolder const & types)
  {
    for (size_t i = 0; i < ARRAY_SIZE(m_types); ++i)
      if (types.Has(m_types[i]))
        return true;
    return false;
  }
};

double GetDistanceMeters(m2::PointD const & p1, m2::PointD const & p2)
{
  return ms::DistanceOnEarth(MercatorBounds::YToLat(p1.y), MercatorBounds::XToLon(p1.x),
                             MercatorBounds::YToLat(p2.y), MercatorBounds::XToLon(p2.x));
}

class ProjectionCalcToStreet
{
  vector<m2::PointD> const & m_points;
  double m_distanceMeters;

  typedef m2::ProjectionToSection<m2::PointD> ProjectionT;
  vector<ProjectionT> m_calcs;

public:
  ProjectionCalcToStreet(Street const * st, double distanceMeters)
    : m_points(st->m_points), m_distanceMeters(distanceMeters)
  {
  }

  void Initialize()
  {
    if (m_calcs.empty() && !m_points.empty())
    {
      m_calcs.reserve(m_points.size() - 1);
      for (size_t i = 1; i < m_points.size(); ++i)
      {
        m_calcs.push_back(ProjectionT());
        m_calcs.back().SetBounds(m_points[i-1], m_points[i]);
      }
    }
  }

  void CalculateProjectionParameters(m2::PointD const & pt,
                                     m2::PointD & resPt, double & dist, double & resDist, size_t & ind)
  {
    for (size_t i = 0; i < m_calcs.size(); ++i)
    {
      m2::PointD const proj = m_calcs[i](pt);
      dist = GetDistanceMeters(pt, proj);
      if (dist < resDist)
      {
        resPt = proj;
        resDist = dist;
        ind = i;
      }
    }
  }

  bool GetProjection(m2::PointD const & pt, HouseProjection & proj)
  {
    Initialize();

    m2::PointD resPt;
    double dist = numeric_limits<double>::max();
    double resDist = numeric_limits<double>::max();
    size_t ind;

    CalculateProjectionParameters(pt, resPt, dist, resDist, ind);

    if (resDist <= m_distanceMeters)
    {
      proj.m_proj = resPt;
      proj.m_distance = resDist;

      proj.m_streetDistance = 0.0;
      for (size_t i = 0; i < ind; ++i)
        proj.m_streetDistance += m_calcs[i].GetLength();
      proj.m_streetDistance += m_points[ind].Length(proj.m_proj);

      proj.m_projectionSign = m2::GetOrientation(m_points[ind], m_points[ind+1], pt) >= 0;
      return true;
    }
    else
      return false;
  }
};

}

template <class ProjectionCalcT>
void HouseDetector::ReadHouse(FeatureType const & f, Street * st, ProjectionCalcT & calc)
{
  static HouseChecker checker;
  if (checker.IsHouse(feature::TypesHolder(f)) && !f.GetHouseNumber().empty())
  {
    map<FeatureID, House *>::iterator const it = m_id2house.find(f.GetID());

    // 15 - is a minimal building scale (enough for center point).
    m2::PointD const pt = (it == m_id2house.end()) ?
          f.GetLimitRect(15).Center() : it->second->GetPosition();

    HouseProjection pr;
    if (calc.GetProjection(pt, pr))
    {
      House * p;
      if (it == m_id2house.end())
      {
        p = new House(f.GetHouseNumber(), pt);
        m_id2house[f.GetID()] = p;
      }
      else
      {
        p = it->second;
        ASSERT(p != 0, ());
      }

      pr.m_house = p;
      st->m_houses.push_back(pr);
    }
  }
}

void HouseDetector::ReadHouses(Street * st, double offsetMeters)
{
  if (st->m_housesReaded)
    return;

  ProjectionCalcToStreet calcker(st, offsetMeters);
  m_loader.ForEachInRect(st->GetLimitRect(offsetMeters),
                         bind(&HouseDetector::ReadHouse<ProjectionCalcToStreet>, this, _1, st, ref(calcker)));

  st->SortHousesProjection();
  st->m_housesReaded = true;
}

void HouseDetector::ReadAllHouses(double offsetMeters)
{
  for (map<FeatureID, Street *>::iterator it = m_id2st.begin(); it != m_id2st.end(); ++it)
    ReadHouses(it->second, offsetMeters);
}

void HouseDetector::MatchAllHouses(string const & houseNumber, vector<HouseProjection> & res)
{
  /// @temporary decision to avoid duplicating houses
  set<House const *> s;

  for (IterM it = m_id2st.begin(); it != m_id2st.end();++it)
  {
    Street const * st = it->second;

    for (size_t i = 0; i < st->m_houses.size(); ++i)
    {
      House const * house = st->m_houses[i].m_house;
      if (house->GetNumber() == houseNumber && s.count(house) == 0)
      {
        res.push_back(st->m_houses[i]);
        s.insert(house);
      }
    }
  }
}

void HouseDetector::ClearCaches()
{
  for (IterM it = m_id2st.begin(); it != m_id2st.end();++it)
    delete it->second;
  m_id2st.clear();

  for (map<FeatureID, House *>::iterator it = m_id2house.begin(); it != m_id2house.end(); ++it)
    delete it->second;

  m_streetNum = 0;

  m_id2house.clear();
  m_end2st.clear();
  m_streets.clear();
}

namespace
{

struct LS
{
  size_t prevDecreasePos, decreaseValue;
  size_t prevIncreasePos, increaseValue;

  LS() {}
  LS(size_t i)
  {
    prevDecreasePos = i;
    decreaseValue = 1;
    prevIncreasePos = i;
    increaseValue = 1;
  }
};

void LongestSubsequence(vector<HouseProjection> const & houses,
                        vector<HouseProjection> & result)
{
  if (houses.size() < 2)
  {
    result = houses;
    return;
  }

  vector<LS> v(houses.size());
  for (size_t i = 0; i < v.size(); ++i)
    v[i] = LS(i);

  House::LessHouseNumber cmp;

  size_t res = 0;
  size_t pos = 0;
  for (size_t i = 0; i + 1 < houses.size(); ++i)
  {
    for (size_t j = i + 1; j < houses.size(); ++j)
    {
      if (cmp(houses[i].m_house, houses[j].m_house) && v[i].increaseValue + 1 > v[j].increaseValue)
      {
        v[j].increaseValue = v[i].increaseValue + 1;
        v[j].prevIncreasePos = i;
      }
      if (!cmp(houses[i].m_house, houses[j].m_house) && v[i].decreaseValue + 1 > v[j].decreaseValue)
      {
        v[j].decreaseValue = v[i].decreaseValue + 1;
        v[j].prevDecreasePos = i;
      }

      size_t const m = max(v[j].increaseValue, v[j].decreaseValue);
      if (m > res)
      {
        res = m;
        pos = j;
      }
    }
  }

  result.resize(res);
  bool increasing = true;
  if (v[pos].increaseValue < v[pos].decreaseValue)
    increasing = false;

  while (res > 0)
  {
    result[res - 1] = houses[pos];
    --res;
    if (increasing)
      pos = v[pos].prevIncreasePos;
    else
      pos = v[pos].prevDecreasePos;
  }
}

typedef map<search::House const *, double, House::LessHouseNumber> HouseMapT;

void AddHouseToMap(search::HouseProjection const & proj, HouseMapT & m)
{
  HouseMapT::iterator it = m.find(proj.m_house);
  if (it != m.end())
  {
    if (it->second > proj.m_distance)
      m.erase(it);
    else
      return;
  }
  m.insert(make_pair(proj.m_house, proj.m_distance));
}

bool CheckOddEven(search::HouseProjection const & h, bool isOdd)
{
  int const x = h.m_house->GetIntNumber();
  return ((x % 2 == 1) == isOdd);
}

void CreateKMLString(ostream & s, map<search::House, double> const & m)
{
  for (map<search::House, double>::const_iterator it = m.begin(); it != m.end(); ++it)
  {
    s << "<Placemark>"
      << "<name>" << it->first.GetNumber() <<  "</name>"

      << "<Point><coordinates>"
            << MercatorBounds::XToLon(it->first.GetPosition().x)
            << ","
            << MercatorBounds::YToLat(it->first.GetPosition().y)

      << "</coordinates></Point>"
      << "</Placemark>" << endl;
  }
}

void ProccessHouses(vector<search::HouseProjection> & houses,
                    vector<search::HouseProjection> & result,
                    bool isOdd, HouseMapT & m)
{
  houses.erase(remove_if(houses.begin(), houses.end(), bind(&CheckOddEven, _1, isOdd)), houses.end());

  result.clear();
  LongestSubsequence(houses, result);

  for_each(result.begin(), result.end(), bind(&AddHouseToMap, _1, ref(m)));
}

House const * GetClosestHouse(vector<Street *> const & st, string const & houseNumber)
{
  double dist = numeric_limits<double>::max();
  int streetIndex = -1;
  int houseIndex = -1;

  for (size_t i = 0; i < st.size(); ++i)
  {
    for (size_t j = 0; j < st[i]->m_houses.size(); ++j)
    {
      if (st[i]->m_houses[j].m_house->GetNumber() == houseNumber)
      {
        if (st[i]->m_houses[j].m_distance < dist)
        {
          dist = st[i]->m_houses[j].m_distance;
          streetIndex = i;
          houseIndex = j;
        }
      }
    }
  }

  if (streetIndex == -1)
    return 0;
  return st[streetIndex]->m_houses[houseIndex].m_house;
}

House const * GetLSHouse(vector<search::Street *> const & st, string const & houseNumber)
{
  HouseMapT m;
  for (size_t i = 0; i < st.size(); ++i)
  {
    vector<search::HouseProjection> left, right;

    double resDist = numeric_limits<double>::max();
    size_t resInd = numeric_limits<size_t>::max();
    for (size_t j = 0; j < st[i]->m_houses.size(); ++j)
    {
      search::HouseProjection const & projection = st[i]->m_houses[j];
      if (projection.m_projectionSign)
        right.push_back(projection);
      else
        left.push_back(projection);

      // Skip houses with projection on street ends.
      double const dist = projection.m_distance;
      if (resDist > dist &&
          projection.m_proj != st[i]->m_points.front() && projection.m_proj != st[i]->m_points.back())
      {
        resInd = j;
        resDist = dist;
      }
    }

    if (resInd >= st[i]->m_houses.size())
      continue;

    bool leftOdd, rightOdd;
    if (!st[i]->m_houses[resInd].m_projectionSign)
    {
      rightOdd = false;
      if (st[i]->m_houses[resInd].m_house->GetIntNumber() % 2 == 1)
        rightOdd = true;
      leftOdd = !rightOdd;
    }
    else
    {
      leftOdd = false;
      if (st[i]->m_houses[resInd].m_house->GetIntNumber() % 2 == 1)
        leftOdd = true;
      rightOdd = !leftOdd;
    }

    vector<search::HouseProjection> leftRes, rightRes;
    ProccessHouses(right, rightRes, rightOdd, m);
    ProccessHouses(left, leftRes, leftOdd, m);
  }

  for (HouseMapT::iterator it = m.begin(); it != m.end(); ++it)
  {
    /// @todo Compare result house smarter (may skip some suffixes or prefixes).
    if (it->first->GetNumber() == houseNumber)
      return it->first;
  }
  return 0;
}

}

void HouseDetector::GetHouseForName(string const & houseNumber, vector<House const *> & res)
{
  size_t const count = m_streets.size();
  res.reserve(count);

  for (size_t i = 0; i < count; ++i)
  {
    House const * house = GetLSHouse(m_streets[i], houseNumber);
    if (house == 0)
      house = GetClosestHouse(m_streets[i], houseNumber);

    if (house)
      res.push_back(house);
  }
}

}
