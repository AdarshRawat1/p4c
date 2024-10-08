#ifndef BACKENDS_P4TOOLS_COMMON_LIB_UTIL_H_
#define BACKENDS_P4TOOLS_COMMON_LIB_UTIL_H_

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <boost/random/mersenne_twister.hpp>

#include "ir/ir.h"

namespace P4::P4Tools {

/// General utility functions that are not present in the compiler framework.
class Utils {
    /* =========================================================================================
     *  Seeds, timestamps, randomness.
     * ========================================================================================= */
 private:
    /// The random generator of this project. It is initialized with the input seed.
    static boost::random::mt19937 rng;

    /// Stores the state of the PRNG.
    static std::optional<uint32_t> currentSeed;

 public:
    /// Return the current timestamp with millisecond accuracy.
    /// Format: year-month-day-hour:minute:second.millisecond
    /// Borrowed from https://stackoverflow.com/a/35157784
    static std::string getTimeStamp();

    /// Initialize the random generator with an integer seed. This also seeds @var currentSeed.
    /// Uses boost's mersenne twister.
    static void setRandomSeed(int seed);

    /// @returns currentSeed.
    static std::optional<uint32_t> getCurrentSeed();

    /// @returns a random integer in the range [0, @param max]. Always return 0 if no seed is set.
    static uint64_t getRandInt(uint64_t max);

    /// @returns a random integer between min and max.
    static int64_t getRandInt(int64_t min, int64_t max);

    /// @returns a random integer based on the percent vector.
    static int64_t getRandInt(const std::vector<int64_t> &percent);

    /// @returns a random big integer in the range [0, @param max]. Always return 0 if no seed is
    /// set.
    static big_int getRandBigInt(const big_int &max);

    /// This is a big_int version of getRndInt.
    static big_int getRandBigInt(const big_int &min, const big_int &max);

    /// @returns a IR::Constant with a random big integer that fits the specified bit width.
    /// The type will be an unsigned Type_Bits with @param bitWidth.
    static const IR::Constant *getRandConstantForWidth(int bitWidth);

    /// @returns a IR::Constant with a random big integer that fits the specified @param type.
    static const IR::Constant *getRandConstantForType(const IR::Type_Bits *type);

    /* =========================================================================================
     *  Other.
     * ========================================================================================= */
 public:
    /// @returns a method call to an internal extern consumed by the interpreter. The return type
    /// is typically Type_Void.
    static const IR::MethodCallExpression *generateInternalMethodCall(
        std::string_view methodName, const std::vector<const IR::Expression *> &argVector,
        const IR::Type *returnType = IR::Type_Void::get(),
        const IR::ParameterList *paramList = new IR::ParameterList());

    /// Shuffles the given iterable @param inp
    template <typename T>
    static void shuffle(T *inp) {
        std::shuffle(inp->begin(), inp->end(), rng);
    }

    /// @returns a random element from the given range between @param start and @param end.
    template <typename Iter>
    static Iter pickRandom(Iter start, Iter end) {
        int random = getRandInt(std::distance(start, end) - 1);
        std::advance(start, random);
        return start;
    }

    /// Convert a container type (array, set, vector, etc.) into a well-formed [val1, val2, ...]
    /// representation. This function is used for debugging output.
    template <typename ContainerType>
    static std::string containerToString(const ContainerType &container) {
        std::stringstream stringStream;

        stringStream << '[';
        auto val = container.cbegin();
        if (val != container.cend()) {
            stringStream << *val++;
            while (val != container.cend()) {
                stringStream << ", " << *val++;
            }
        }
        stringStream << ']';
        return stringStream.str();
    }
};

/// Converts the list of arguments @inputArgs to a list of type declarations. Any names appearing in
/// the arguments are resolved with @ns.
/// This is mainly useful for inspecting package instances.
std::vector<const IR::Type_Declaration *> argumentsToTypeDeclarations(
    const IR::IGeneralNamespace *ns, const IR::Vector<IR::Argument> *inputArgs);

/// Looks up a declaration from a path. A BUG occurs if no declaration is found.
const IR::IDeclaration *findProgramDecl(const IR::IGeneralNamespace *ns, const IR::Path *path);

/// Looks up a declaration from a path expression. A BUG occurs if no declaration is found.
const IR::IDeclaration *findProgramDecl(const IR::IGeneralNamespace *ns,
                                        const IR::PathExpression *pathExpr);

/// Resolves a Type_Name in the top-level namespace.
const IR::Type_Declaration *resolveProgramType(const IR::IGeneralNamespace *ns,
                                               const IR::Type_Name *type);

}  // namespace P4::P4Tools

#endif /* BACKENDS_P4TOOLS_COMMON_LIB_UTIL_H_ */
