// The translation unit Blueprint reads when it documents itself.
//
// It includes every public header of the tool and nothing else, so the model
// extracted from it is the design of Blueprint as its own interface declares
// it. A tool that cannot draw its own structure is not one to trust with
// anybody else's.
#pragma once

#include "blueprint/behavior.hpp"
#include "blueprint/diagram.hpp"
#include "blueprint/diff.hpp"
#include "blueprint/emit.hpp"
#include "blueprint/io.hpp"
#include "blueprint/json.hpp"
#include "blueprint/model.hpp"
#include "blueprint/reader.hpp"
#include "blueprint/schema.hpp"
