#pragma once
#include <variant>
#include <utility>

// ─── Purely generic State Machine — no virtual functions ───────────────────
//
// Uses std::variant for compile-time polymorphism.
//
// How to use
// ----------
// 1. Forward-declare all state structs.
// 2. Define:  using MyStates = std::variant<StateA, StateB, ...>;
// 3. Implement each struct:
//      struct StateA {
//          MyStates update(Context& ctx) {
//              if (someCondition(ctx)) return StateB{};   // transition
//              return *this;                               // stay
//          }
//          void onEnter(Context& ctx) { ... }
//          void onExit (Context& ctx) { ... }
//      };
// 4. Instantiate:
//      StateMachine<Context, StateA, StateB> sm(ctx, StateA{});
//      sm.update();  // call every tick
// ───────────────────────────────────────────────────────────────────────────

template <typename TContext, typename... TStates>
class StateMachine {
public:
    using StateVariant = std::variant<TStates...>;

    StateMachine(TContext& ctx, StateVariant initial)
        : _ctx(ctx), _current(std::move(initial))
    {
        enterCurrent();
    }

    void update() {
        StateVariant next = std::visit(
            [&](auto& s) -> StateVariant { return s.update(_ctx); },
            _current
        );
        if (next.index() != _current.index()) {
            exitCurrent();
            _current = std::move(next);
            enterCurrent();
        }
    }

    template <typename TState>
    void transitionTo(TState next = {}) {
        exitCurrent();
        _current = StateVariant(std::move(next));
        enterCurrent();
    }

    template <typename TState>
    bool isIn() const { return std::holds_alternative<TState>(_current); }

    template <typename TState>
    TState& get() { return std::get<TState>(_current); }

private:
    void enterCurrent() {
        std::visit([&](auto& s) { s.onEnter(_ctx); }, _current);
    }
    void exitCurrent() {
        std::visit([&](auto& s) { s.onExit(_ctx); }, _current);
    }

    TContext&    _ctx;
    StateVariant _current;
};
