// sjtu_interpretutil.cpp
#include <sjtu_interpretutil.h>

#include <bdlma_localsequentialallocator.h>

#include <bsl_iostream.h>
#include <bsl_vector.h>
#include <bsls_assert.h>

#include <sjtt_bytecode.h>
#include <sjtt_executioncontext.h>
#include <sjtt_datumudtutil.h>

using namespace BloombergLP;

namespace sjtu {

namespace {
struct Frame {
    BSLMF_NESTED_TRAIT_DECLARATION(Frame, bslma::UsesBslmaAllocator);

    bsl::vector<bdld::Datum>  stack;
    const sjtt::Bytecode     *pc;

    explicit Frame(bslma::Allocator *alloc) : stack(alloc), pc(0) {}
    Frame(const Frame& rhs, bslma::Allocator *alloc)
        : stack(rhs.stack, alloc)
        , pc(rhs.pc) {
    }
};
}

bdld::Datum
InterpretUtil::interpretBytecode(Allocator            *allocator,
                                 const sjtt::Bytecode *firstCode,
                                 int                   numCodes) {
    BSLS_ASSERT(0 != allocator);
    BSLS_ASSERT(0 != firstCode || 0 == numCodes);

    bdlma::LocalSequentialAllocator<2046> tempAlloc(allocator);
    bsl::vector<Frame> frames(&tempAlloc);
    frames.resize(1);
    frames.back().pc = firstCode;

    bdld::Datum locals[sjtt::Bytecode::s_NumLocals];

#ifdef BSLS_ASSERT_IS_ACTIVE
    int i = 0;
#endif

    Frame *frame = &frames.back();
    bsl::vector<bdld::Datum> *stack = &frame->stack;
    const sjtt::Bytecode *pc = frame->pc;
    while (true) {
        const sjtt::Bytecode& code = *pc;
        switch (code.opcode()) {

          case sjtt::Bytecode::e_Push: {

            stack->push_back(code.data());
          } break;

          case sjtt::Bytecode::e_Load: {

            BSLS_ASSERT(code.data().isInteger());
            BSLS_ASSERT(sjtt::Bytecode::s_NumLocals >
                        code.data().theInteger());

            stack->push_back(locals[code.data().theInteger()]);
          } break;

          case sjtt::Bytecode::e_Store: {

            BSLS_ASSERT(code.data().isInteger());
            BSLS_ASSERT(sjtt::Bytecode::s_NumLocals >
                        code.data().theInteger());
            locals[code.data().theInteger()] = stack->back();
            stack->pop_back();
          } break;

          case sjtt::Bytecode::e_Jump: {

            BSLS_ASSERT(code.data().isInteger());
            BSLS_ASSERT(numCodes > code.data().theInteger());

            pc = firstCode + code.data().theInteger();
            continue;
          } break;

          case sjtt::Bytecode::e_Gosub: {

            BSLS_ASSERT(code.data().isInteger());
            BSLS_ASSERT(numCodes > code.data().theInteger());

            const int offset = (pc + 1) - firstCode;
            stack->push_back(bdld::Datum::createInteger(offset));
            pc = firstCode + code.data().theInteger();
            continue;
          } break;

          case sjtt::Bytecode::e_Return: {

            BSLS_ASSERT(!stack->empty());
            BSLS_ASSERT(stack->back().isInteger());
            BSLS_ASSERT(numCodes > stack->back().theInteger());

            pc = firstCode + stack->back().theInteger();
            stack->pop_back();
            continue;
          } break;

          case sjtt::Bytecode::e_AddDoubles: {

            BSLS_ASSERT(1 < stack->size());
            BSLS_ASSERT(stack->back().isDouble());
            BSLS_ASSERT((*stack)[stack->size() - 2].isDouble());

            const double l = stack->back().theDouble();
            stack->pop_back();
            const double result = l + stack->back().theDouble();
            stack->pop_back();
            stack->push_back(bdld::Datum::createDouble(result));
          } break;

          case sjtt::Bytecode::e_Call: {
            BSLS_ASSERT(!stack->empty());
            BSLS_ASSERT(stack->back().isInteger());
            BSLS_ASSERT(stack->size() > stack->back().theInteger() + 1);
            BSLS_ASSERT(code.data().isInteger());
            BSLS_ASSERT(numCodes > code.data().theInteger());

            const int argCount = stack->back().theInteger();

            // Get the arguments on the stack for new frame.

            frames.resize(frames.size() + 1);
            frame = &frames.back();
            frame->stack.assign(stack->end() - (1 + argCount),
                                stack->end() - 1);

            // Pop them off the old one

            stack->erase(stack->end() - (1 + argCount), stack->end());

            // set up frame variables

            stack = &frame->stack;
            pc    = firstCode + code.data().theInteger();
            continue;                             // skip past normal increment
          } break;

          case sjtt::Bytecode::e_Execute: {

            BSLS_ASSERT(!stack->empty());
            BSLS_ASSERT(sjtt::DatumUdtUtil::isExternalFunction(stack->back()));

            const sjtt::DatumUdtUtil::ExternalFunction f =
                        sjtt::DatumUdtUtil::getExternalFunction(stack->back());
            stack->pop_back();
            BSLS_ASSERT(stack->back().isInteger());
            const int numArgs = stack->back().theInteger();
            stack->pop_back();
            const Datum *end = stack->end();
            const Datum *firstArg = end - numArgs;
            const Datum result =
                      f(sjtt::ExecutionContext(&tempAlloc, firstArg, numArgs));
            stack->erase(firstArg, end);
            stack->push_back(result);
          } break;

          case sjtt::Bytecode::e_Exit: {

            BSLS_ASSERT(!stack->empty());

            const Datum value = stack->back();
            if (1 == frames.size()) {
                // If last frame, return the value.

                return value.clone(allocator);                // RETURN
            }
            else {
                // Otherwise, pop the frame and push the last value

                frames.pop_back();
                frame = &frames.back();
                stack = &frame->stack;
                stack->push_back(value);
                pc = frame->pc;  // will increment to next code
            }
          } break;
        }
        ++pc;
        BSLS_ASSERT(++i < numCodes);
    }
}
}
