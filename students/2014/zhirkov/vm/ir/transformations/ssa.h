#pragma once

#include <map>
#include <set>
#include "identity.h"

namespace mathvm {
    namespace IR {
        struct SsaTransformation : public IdentityTransformation {
            std::map<uint64_t, uint64_t> _latestVersion;
            SsaTransformation(SimpleIr& old, std::ostream& debug=std::cerr)
                    : IdentityTransformation(old, "ssa transformation", debug),
                      meta(_currentIr->varMeta) {
            }

            bool shouldBeRenamed(uint64_t id) {
                return _latestVersion.find(id) != _latestVersion.end();
            }
            uint64_t newName(uint64_t id) {
                return _latestVersion[id];
            }

            FOR_IR(VISITOR);

        private:
            std::vector<IR::SimpleIr::VarMeta>& meta;
        };

    }
}