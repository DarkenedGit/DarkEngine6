#pragma once

// Hierarchical state machine (UML-style subset for game / AI use).
//
//   #include "AI/Hsm.h"
//
// Features: state nesting, nested entry/exit actions, enter/exit guards,
// transition guards, shallow/deep history, shared transitions, leaf-to-root
// event propagation, and behavior inheritance via HsmState::setBase().

#include "AI/HsmTypes.h"
#include "AI/HsmState.h"
#include "AI/HsmMachine.h"
