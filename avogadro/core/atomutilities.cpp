/******************************************************************************
  This source file is part of the Avogadro project.
  This source code is released under the 3-Clause BSD License, (see "LICENSE").
******************************************************************************/

#include "atomutilities.h"

#include "../core/mdlvalence_p.h"

#include <algorithm>
#include <cmath>
#include <vector>

#define M_TETRAHED 109.47122063449069389

using Avogadro::Vector3;
using Avogadro::Core::Array;
using Avogadro::Core::atomValence;
using Avogadro::Core::Atom;
using Avogadro::Core::Bond;
using Avogadro::Core::Molecule;

namespace {

typedef Array<Bond> NeighborListType;

// Return the other atom in the bond.
inline Atom getOtherAtom(const Atom& atom, const Bond& bond)
{
  return bond.atom1().index() != atom.index() ? bond.atom1() : bond.atom2();
}

inline unsigned int countExistingBonds(const NeighborListType& bonds)
{
  unsigned int result(0);
  for (NeighborListType::const_iterator it = bonds.begin(), itEnd = bonds.end();
       it != itEnd; ++it) {
    result += static_cast<unsigned int>(it->order());
  }
  return result;
}

} // end anon namespace

namespace Avogadro {
namespace Core {

Avogadro::Core::AtomHybridization AtomUtilities::perceiveHybridization(const Atom& atom)
{
  const NeighborListType bonds(atom.molecule()->bonds(atom));
  const unsigned int numberOfBonds(countExistingBonds(bonds)); // bond order sum

  AtomHybridization hybridization = SP3; // default to sp3

  // TODO: Handle hypervalent species, SO3, SO4, lone pairs, etc.

  if (numberOfBonds > 4) {
    //      hybridization = numberOfBonds; // e.g., octahedral, trig. bipyr.,
    //      etc.
  } else {
    // Count multiple bonds
    unsigned int numTripleBonds = 0;
    unsigned int numDoubleBonds = 0;

    for (NeighborListType::const_iterator it = bonds.begin(),
                                          itEnd = bonds.end();
         it != itEnd; ++it) {
      if (it->order() == 2)
        numDoubleBonds++;
      else if (it->order() == 3)
        numTripleBonds++;
    }

    if (numTripleBonds > 0 || numDoubleBonds > 1)
      hybridization = Core::SP; // sp
    else if (numDoubleBonds > 0)
      hybridization = Core::SP2; // sp2
  }

  return hybridization;
}

// Generate bond geometries
// First, the default fallback (random vectors)
// Also applies when you have a linear geometry and just need one new vector
// (it doesn't matter where it goes).
Vector3 AtomUtilities::generateNewBondVector(
  const Atom& atom, std::vector<Vector3>& allVectors,
  Core::AtomHybridization hybridization)
{
  Vector3 newPos;
  bool success = false;
  int currentValence = allVectors.size();

  // No bonded atoms, just pick a random vector
  if (currentValence == 0) {
    newPos = Vector3::Random().normalized();
    return newPos;
  } else if (currentValence == 1) {
    // One bonded atom
    Vector3 bond1 = allVectors[0];

    // Check what's attached to our neighbor -- we want to set trans to the
    // neighbor
    Vector3 bond2(0.0, 0.0, 0.0);

    const NeighborListType bonds(atom.molecule()->bonds(atom));
    for (NeighborListType::const_iterator it = bonds.begin(),
                                          itEnd = bonds.end();
         it != itEnd; ++it) {
      Atom a1 = getOtherAtom(atom, *it);
      const NeighborListType nbrBonds(atom.molecule()->bonds(a1));
      for (NeighborListType::const_iterator nbIt = nbrBonds.begin(),
                                            nbItEnd = nbrBonds.end();
           nbIt != nbItEnd; ++nbIt) {
        Atom a2 = getOtherAtom(a1, *nbIt);
        if (a2.index() == atom.index())
          continue; // we want a *new* atom

        Vector3 delta = a2.position3d() - a1.position3d();
        if (!delta.isZero(1e-5))
          bond2 = delta.normalized();

        // Check for carboxylate (CO2)
        if ((atom.atomicNumber() == 8)  // atom for H is O
            && (a1.atomicNumber() == 6) // central atom is C
            && (nbIt->order() == 2) && (a2.atomicNumber() == 8))
          break; // make sure the H will be trans to the C=O
      }
    }

    Vector3 v1, v2;
    v1 = bond1.cross(bond2);
    bool noA2 = false;
    if (bond2.norm() < 1.0e-5 || v1.norm() < 1.0e-5) {
      //        std::cout << " creating a random paired atom " << std::endl;

      // there is no a-2 atom
      noA2 = true;
      v2 = Vector3::Random().normalized();

      double angle = fabs(acos(bond1.dot(v2)));
      while (angle < 45.0 * DEG_TO_RAD || angle > 135.0 * DEG_TO_RAD) {
        v2 = Vector3::Random().normalized();
        angle = fabs(acos(bond1.dot(v2)));
        //          std::cout << "angle = " << angle*RAD_TO_DEG << std::endl;
      }
      v1 = bond1.cross(v2); // so find a perpendicular, given the random vector
      v2 = bond1.cross(v1);
    } else {
      //        std::cout << " found a neighbor for trans " << std::endl;
      v1 = bond1.cross(bond2);
      v2 = -1.0 * bond1.cross(v1);
    }
    v2.normalize();

    switch (hybridization) {
      case Core::SP:
      case Core::SquarePlanar:
      case Core::TrigonalBipyramidal:
        newPos = bond1; // 180 degrees away from the current neighbor
        break;
      case Core::SP2: // sp2
        newPos = bond1 - v2 * tan(DEG_TO_RAD * 120.0);
        break;
      case Core::Octahedral: // octahedral
        newPos = bond1 - v2 * tan(DEG_TO_RAD * 90.0);
        break;
      case Core::SP3:
      default:
        newPos = (bond1 - v2 * tan(DEG_TO_RAD * M_TETRAHED));
        break;
    }

    //      std::cout << " one bond " << newPos.normalized() << std::endl;
    return -1.0 * newPos.normalized();
  } // end one bond
  else if (currentValence == 2) {
    Vector3 bond1 = allVectors[0];
    Vector3 bond2 = allVectors[1];

    Vector3 v1 = bond1 + bond2;
    v1.normalize();

    switch (hybridization) {
      case Core::SP: // shouldn't happen, but maybe with metal atoms?
      case Core::SP2:
        newPos = v1; // point away from the two existing bonds
        break;
      case Core::SP3:
      default:
        Vector3 v2 = bond1.cross(bond2); // find the perpendicular
        v2.normalize();
        //newPos = bond1 - v2 * tan(DEG_TO_RAD * (M_TETRAHED));
        newPos = v2 + v1 * (sqrt(2.0) / 2.0);
    }

    //      std::cout << " two bonds " << newPos.normalized() << std::endl;
    return -1.0 * newPos.normalized();
  } // end two bonds
  else if (currentValence == 3) {
    Vector3 bond1 = allVectors[0];
    Vector3 bond2 = allVectors[1];
    Vector3 bond3 = allVectors[2];

    // need to handle different hybridizations here

    // since the base of the tetrahedron should be symmetric
    // the sum of the three bond vectors should cancel the angular parts
    // and point in the new direction.. just need to normalize and rescale
    newPos = -1.0 * (bond1 + bond2 + bond3);

    //      std::cout << " three bonds " << newPos.normalized() << std::endl;
    return newPos.normalized();
  }

  // Fallback:
  // Try 10 times to generate a random vector that doesn't overlap with
  // an existing bond. If we can't, just give up and let the overlap occur.

  // Tolerance for two vectors being "too close" in radians (pi/8).
  const Avogadro::Real cosRadTol =
    cos(static_cast<Avogadro::Real>(M_PI) / static_cast<Avogadro::Real>(8.));

  for (int attempt = 0; !success && attempt < 10; ++attempt) {
    newPos = Vector3::Random().normalized();
    success = true;
    for (std::vector<Vector3>::const_iterator it = allVectors.begin(),
                                              itEnd = allVectors.end();
         success && it != itEnd; ++it) {
      success = newPos.dot(*it) < cosRadTol;
    }
  }
  return newPos;
}

} // namespace Core
} // namespace Avogadro
