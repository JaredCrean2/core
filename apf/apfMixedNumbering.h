/*
 * Copyright 2011 Scientific Computation Research Center
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef APF_MIXED_NUMBERING_H
#define APF_MIXED_NUMBERING_H

/** \file apfMixedNumbering.h
  * \brief Global numbering interface for mixed fields.
  * \details This is separate from apfNumbering.h because it pulls
  * in std::vector. */

#include "apf.h"
#include <vector>

namespace apf {

typedef NumberingOf<long> GlobalNumbering;

/** \brief Number owned nodes of multiple fields.
  * \param fields The input fields to be numbered.
  * \param n The output global numberings corresponding to the fields.
  * \returns The number of owned nodes across all fields. */
int numberMixed(
    std::vector<Field*> const& fields,
    std::vector<GlobalNumbering*>& n);

}

#endif
