#ifndef _CPPROMISE_HPP
#define _CPPROMISE_HPP

/**
 * MIT License
 * Copyright (c) 2018 Ted Yin <tederminant@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stack>
#include <vector>
#include <memory>
#include <functional>
#include <type_traits>

#if __cplusplus >= 201703L
#ifdef __has_include
#   if __has_include(<any>)
#       include <any>
#       ifdef __cpp_lib_any
#           define _CPPROMISE_STD_ANY
#       endif
#   endif
#endif
#endif

#ifndef _CPPROMISE_STD_ANY
#include <boost/any.hpp>
#endif

/**
 * Implement type-safe Promise primitives similar to the ones specified by
 * Javascript Promise/A+.
 */
namespace promise {
#ifdef _CPPROMISE_STD_ANY
    using pm_any_t = std::any;
    template<typename T>
    constexpr auto any_cast = static_cast<T(*)(const std::any&)>(std::any_cast<T>);
    using bad_any_cast = std::bad_any_cast;
#else
#   warning "using boost::any"
#   pragma message "using boost::any"
    using pm_any_t = boost::any;
    template<typename T>
    constexpr auto any_cast = static_cast<T(*)(const boost::any&)>(boost::any_cast<T>);
    using bad_any_cast = boost::bad_any_cast;
#endif
    using callback_t = std::function<void()>;
    using values_t = std::vector<pm_any_t>;

    /* match lambdas */
    template<typename T>
    struct function_traits_impl:
        public function_traits_impl<decltype(&T::operator())> {};
 
    template<typename ReturnType>
    struct function_traits_impl<ReturnType()> {
        using ret_type = ReturnType;
        using arg_type = void;
        using empty_arg = void;
    };
   
    /* match plain functions */
    template<typename ReturnType, typename ArgType>
    struct function_traits_impl<ReturnType(ArgType)> {
        using ret_type = ReturnType;
        using arg_type = ArgType;
        using non_empty_arg = void;
    };
 
    /* match function pointers */
    template<typename ReturnType, typename... ArgType>
    struct function_traits_impl<ReturnType(*)(ArgType...)>:
        public function_traits_impl<ReturnType(ArgType...)> {};

    /* match const member functions */
    template<typename ClassType, typename ReturnType, typename... ArgType>
    struct function_traits_impl<ReturnType(ClassType::*)(ArgType...) const>:
        public function_traits_impl<ReturnType(ArgType...)> {};

    /* match member functions */
    template<typename ClassType, typename ReturnType, typename... ArgType>
    struct function_traits_impl<ReturnType(ClassType::*)(ArgType...)>:
        public function_traits_impl<ReturnType(ArgType...)> {};

    template<typename Func>
    struct function_traits:
        public function_traits_impl<typename std::remove_reference_t<Func>> {};

    template<typename Func, typename ReturnType>
    using enable_if_return = typename std::enable_if_t<
            std::is_same<typename function_traits<Func>::ret_type,
                         ReturnType>::value>;

    template<typename Func, typename ReturnType>
    using disable_if_return = typename std::enable_if_t<
            !std::is_same<typename function_traits<Func>::ret_type,
                         ReturnType>::value>;

    template<typename Func, typename ArgType>
    using enable_if_arg = typename std::enable_if_t<
            std::is_same<typename function_traits<Func>::arg_type,
                         ArgType>::value>;

    template<typename Func, typename ArgType>
    using disable_if_arg = typename std::enable_if_t<
            !std::is_same<typename function_traits<Func>::arg_type,
                         ArgType>::value>;

    template<typename T, typename U>
    using disable_if_same_ref = typename std::enable_if_t<
            !std::is_same<
                std::remove_cv_t<std::remove_reference_t<T>>, U>::value>;

    class Promise;
    //class promise_t: public std::shared_ptr<Promise> {
    class promise_t {
        Promise *pm;
        size_t *ref_cnt;
        public:
        friend Promise;
        template<typename PList> friend promise_t all(const PList &promise_list);
        template<typename PList> friend promise_t race(const PList &promise_list);

        inline promise_t();
        inline ~promise_t();
        template<typename Func, disable_if_same_ref<Func, promise_t> * = nullptr>
                inline promise_t(Func &&callback);

        void swap(promise_t &other) {
            std::swap(pm, other.pm);
            std::swap(ref_cnt, other.ref_cnt);
        }

        promise_t &operator=(const promise_t &other) {
            if (this != &other)
            {
                promise_t tmp(other);
                tmp.swap(*this);
            }
            return *this;
        }

        promise_t &operator=(promise_t &&other) {
            if (this != &other)
            {
                promise_t tmp(std::move(other));
                tmp.swap(*this);
            }
            return *this;
        }

        promise_t(const promise_t &other):
            pm(other.pm),
            ref_cnt(other.ref_cnt) {
            ++*ref_cnt;
        }

        promise_t(promise_t &&other):
            pm(other.pm),
            ref_cnt(other.ref_cnt) {
            other.pm = nullptr;
        }

        Promise *operator->() const {
            return pm;
        }

        template<typename T> inline void resolve(T result) const;
        template<typename T> inline void reject(T reason) const;
        inline void resolve() const;
        inline void reject() const;

        template<typename FuncFulfilled>
        inline promise_t then(FuncFulfilled &&on_fulfilled) const;

        template<typename FuncFulfilled, typename FuncRejected>
        inline promise_t then(FuncFulfilled &&on_fulfilled,
                            FuncRejected &&on_rejected) const;

        template<typename FuncRejected>
        inline promise_t fail(FuncRejected &&on_rejected) const;
    };

#define PROMISE_ERR_INVALID_STATE do {throw std::runtime_error("invalid promise state");} while (0)
#define PROMISE_ERR_MISMATCH_TYPE do {throw std::runtime_error("mismatching promise value types");} while (0)
    
    class Promise {
        template<typename PList> friend promise_t all(const PList &promise_list);
        template<typename PList> friend promise_t race(const PList &promise_list);
        std::vector<callback_t> fulfilled_callbacks;
        std::vector<callback_t> rejected_callbacks;
#ifdef CPPROMISE_USE_STACK_FREE
        std::vector<Promise *> fulfilled_pms;
        std::vector<Promise *> rejected_pms;
#endif
        enum class State {
            Pending,
#ifdef CPPROMISE_USE_STACK_FREE
            PreFulfilled,
            PreRejected,
#endif
            Fulfilled,
            Rejected,
        } state;
        pm_any_t result;
        pm_any_t reason;

        void add_on_fulfilled(callback_t &&cb) {
            fulfilled_callbacks.push_back(std::move(cb));
        }

        void add_on_rejected(callback_t &&cb) {
            rejected_callbacks.push_back(std::move(cb));
        }

        template<typename Func,
            typename function_traits<Func>::non_empty_arg * = nullptr>
        static constexpr auto cps_transform(
                Func &&f, const pm_any_t &result, const promise_t &npm) {
            return [&result, npm, f = std::forward<Func>(f)]() mutable {
#ifndef CPPROMISE_USE_STACK_FREE
                f(result)->then(
                    [npm] (pm_any_t result) {npm->resolve(result);},
                    [npm] (pm_any_t reason) {npm->reject(reason);});
#else
                promise_t rpm{f(result)};
                rpm->then(
                    [rpm, npm] (pm_any_t result) {
                        npm->_resolve(result);
                    },
                    [rpm, npm] (pm_any_t reason) {
                        npm->_reject(reason);
                    });
                rpm->_dep_resolve(npm);
                rpm->_dep_reject(npm);
#endif
            };
        }

        template<typename Func,
            typename function_traits<Func>::empty_arg * = nullptr>
        static constexpr auto cps_transform(
                Func &&f, const pm_any_t &, const promise_t &npm) {
            return [npm, f = std::forward<Func>(f)]() mutable {
#ifndef CPPROMISE_USE_STACK_FREE
                f()->then(
                    [npm] (pm_any_t result) {npm->resolve(result);},
                    [npm] (pm_any_t reason) {npm->reject(reason);});
#else
                promise_t rpm{f()};
                rpm->then(
                    [rpm, npm] (pm_any_t result) {
                        npm->_resolve(result);
                    },
                    [rpm, npm] (pm_any_t reason) {
                        npm->_reject(reason);
                    });
                rpm->_dep_resolve(npm);
                rpm->_dep_reject(npm);
#endif
            };
        }

        template<typename Func,
            enable_if_return<Func, promise_t> * = nullptr>
        constexpr auto gen_on_fulfilled(Func &&on_fulfilled, const promise_t &npm) {
            return cps_transform(std::forward<Func>(on_fulfilled), this->result, npm);
        }

        template<typename Func,
            enable_if_return<Func, promise_t> * = nullptr>
        constexpr auto gen_on_rejected(Func &&on_rejected, const promise_t &npm) {
            return cps_transform(std::forward<Func>(on_rejected), this->reason, npm);
        }


        template<typename Func,
            enable_if_return<Func, void> * = nullptr,
            typename function_traits<Func>::non_empty_arg * = nullptr>
        constexpr auto gen_on_fulfilled(Func &&on_fulfilled, const promise_t &npm) {
            return [this, npm,
                    on_fulfilled = std::forward<Func>(on_fulfilled)]() mutable {
                on_fulfilled(result);
                npm->_resolve();
            };
        }

        template<typename Func,
            enable_if_return<Func, void> * = nullptr,
            typename function_traits<Func>::empty_arg * = nullptr>
        constexpr auto gen_on_fulfilled(Func &&on_fulfilled, const promise_t &npm) {
            return [on_fulfilled = std::forward<Func>(on_fulfilled), npm]() mutable {
                on_fulfilled();
                npm->_resolve();
            };
        }

        template<typename Func,
            enable_if_return<Func, void> * = nullptr,
            typename function_traits<Func>::non_empty_arg * = nullptr>
        constexpr auto gen_on_rejected(Func &&on_rejected, const promise_t &npm) {
            return [this, npm,
                    on_rejected = std::forward<Func>(on_rejected)]() mutable {
                on_rejected(reason);
                npm->_reject();
            };
        }

        template<typename Func,
            enable_if_return<Func, void> * = nullptr,
            typename function_traits<Func>::empty_arg * = nullptr>
        constexpr auto gen_on_rejected(Func &&on_rejected, const promise_t &npm) {
            return [npm,
                    on_rejected = std::forward<Func>(on_rejected)]() mutable {
                on_rejected();
                npm->_reject();
            };
        }

        template<typename Func,
            enable_if_return<Func, pm_any_t> * = nullptr,
            typename function_traits<Func>::non_empty_arg * = nullptr>
        constexpr auto gen_on_fulfilled(Func &&on_fulfilled, const promise_t &npm) {
            return [this, npm,
                    on_fulfilled = std::forward<Func>(on_fulfilled)]() mutable {
                npm->_resolve(on_fulfilled(result));
            };
        }

        template<typename Func,
            enable_if_return<Func, pm_any_t> * = nullptr,
            typename function_traits<Func>::empty_arg * = nullptr>
        constexpr auto gen_on_fulfilled(Func &&on_fulfilled, const promise_t &npm) {
            return [npm, on_fulfilled = std::forward<Func>(on_fulfilled)]() mutable {
                npm->_resolve(on_fulfilled());
            };
        }

        template<typename Func,
            enable_if_return<Func, pm_any_t> * = nullptr,
            typename function_traits<Func>::non_empty_arg * = nullptr>
        constexpr auto gen_on_rejected(Func &&on_rejected, const promise_t &npm) {
            return [this, npm, on_rejected = std::forward<Func>(on_rejected)]() mutable {
                npm->_reject(on_rejected(reason));
            };
        }

        template<typename Func,
            enable_if_return<Func, pm_any_t> * = nullptr,
            typename function_traits<Func>::empty_arg * = nullptr>
        constexpr auto gen_on_rejected(Func &&on_rejected, const promise_t &npm) {
            return [npm, on_rejected = std::forward<Func>(on_rejected)]() mutable {
                npm->_reject(on_rejected());
            };
        }

#ifdef CPPROMISE_USE_STACK_FREE
        void _trigger() {
            std::stack<std::tuple<
                std::vector<Promise *>::const_iterator,
                std::vector<Promise *> *,
                Promise *>> s;
            auto push_frame = [&s](Promise *pm) {
                if (pm->state == State::PreFulfilled)
                {
                    pm->state = State::Fulfilled;
                    for (auto &cb: pm->fulfilled_callbacks) cb();
                    s.push(std::make_tuple(pm->fulfilled_pms.begin(),
                                          &pm->fulfilled_pms,
                                          pm));
                }
                else if (pm->state == State::PreRejected)
                {
                    pm->state = State::Rejected;
                    for (auto &cb: pm->rejected_callbacks) cb();
                    s.push(std::make_tuple(pm->rejected_pms.begin(),
                                          &pm->rejected_pms,
                                          pm));
                }
            };
            push_frame(this);
            while (!s.empty())
            {
                auto &u = s.top();
                auto &it = std::get<0>(u);
                auto vec = std::get<1>(u);
                auto pm = std::get<2>(u);
                if (it == vec->end())
                {
                    s.pop();
                    vec->clear();
                    pm->fulfilled_callbacks.clear();
                    pm->rejected_callbacks.clear();
                    continue;
                }
                push_frame(*it++);
            }
        }

        void trigger_fulfill() {
            state = State::PreFulfilled;
            _trigger();
        }

        void trigger_reject() {
            state = State::PreRejected;
            _trigger();
        }

        void _resolve() {
            if (state == State::Pending) state = State::PreFulfilled;
        }

        void _reject() {
            if (state == State::Pending) state = State::PreRejected;
        }

        void _dep_resolve(const promise_t &npm) {
            if (state == State::Pending)
                fulfilled_pms.push_back(npm.pm);
            else
                npm->_trigger();
        }

        void _dep_reject(const promise_t &npm) {
            if (state == State::Pending)
                rejected_pms.push_back(npm.pm);
            else
                npm->_trigger();
        }

        void _resolve(pm_any_t _result) {
            if (state == State::Pending)
            {
                result = _result;
                state = State::PreFulfilled;
            }
        }

        void _reject(pm_any_t _reason) {
            if (state == State::Pending)
            {
                reason = _reason;
                state = State::PreRejected;
            }
        }
#else
        void _resolve() { resolve(); }
        void _reject() { reject(); }
        void _resolve(pm_any_t result) { resolve(result); }
        void _reject(pm_any_t reason) { reject(reason); }

        void trigger_fulfill() {
            state = State::Fulfilled;
            for (const auto &cb: fulfilled_callbacks) cb();
            fulfilled_callbacks.clear();
        }

        void trigger_reject() {
            state = State::Rejected;
            for (const auto &cb: rejected_callbacks) cb();
            rejected_callbacks.clear();
        }
#endif
        public:

        Promise(): state(State::Pending) {}
        ~Promise() {}

        template<typename FuncFulfilled, typename FuncRejected>
        promise_t then(FuncFulfilled &&on_fulfilled,
                      FuncRejected &&on_rejected) {
            switch (state)
            {
                case State::Pending:
                return promise_t([this,
                                on_fulfilled = std::forward<FuncFulfilled>(on_fulfilled),
                                on_rejected = std::forward<FuncRejected>(on_rejected)
                                ](promise_t &npm) {
                    add_on_fulfilled(gen_on_fulfilled(std::move(on_fulfilled), npm));
                    add_on_rejected(gen_on_rejected(std::move(on_rejected), npm));
#ifdef CPPROMISE_USE_STACK_FREE
                    _dep_resolve(npm);
                    _dep_reject(npm);
#endif
                });
                case State::Fulfilled:
                return promise_t([this,
                                on_fulfilled = std::forward<FuncFulfilled>(on_fulfilled)
                                ](promise_t &npm) {
                    gen_on_fulfilled(std::move(on_fulfilled), npm)();
                });
                case State::Rejected:
                return promise_t([this,
                                on_rejected = std::forward<FuncRejected>(on_rejected)
                                ](promise_t &npm) {
                    gen_on_rejected(std::move(on_rejected), npm)();
                });
                default: PROMISE_ERR_INVALID_STATE;
            }
        }

        template<typename FuncFulfilled>
        promise_t then(FuncFulfilled &&on_fulfilled) {
            switch (state)
            {
                case State::Pending:
                return promise_t([this,
                                on_fulfilled = std::forward<FuncFulfilled>(on_fulfilled)
                                ](promise_t &npm) {
                    add_on_fulfilled(gen_on_fulfilled(std::move(on_fulfilled), npm));
                    add_on_rejected([this, npm]() {npm->_reject(reason);});
#ifdef CPPROMISE_USE_STACK_FREE
                    _dep_resolve(npm);
                    _dep_reject(npm);
#endif
                });
                case State::Fulfilled:
                return promise_t([this,
                                on_fulfilled = std::forward<FuncFulfilled>(on_fulfilled)
                                ](promise_t &npm) {
                    gen_on_fulfilled(std::move(on_fulfilled), npm)();
                });
                case State::Rejected:
                return promise_t([this](promise_t &npm) {npm->_reject(reason);});
                default: PROMISE_ERR_INVALID_STATE;
            }
        }
 
        template<typename FuncRejected>
        promise_t fail(FuncRejected &&on_rejected) {
            switch (state)
            {
                case State::Pending:
                return promise_t([this,
                                on_rejected = std::forward<FuncRejected>(on_rejected)
                                ](promise_t &npm) {
                    callback_t ret;
                    add_on_rejected(gen_on_rejected(std::move(on_rejected), npm));
                    add_on_fulfilled([this, npm]() {npm->_resolve(result);});
#ifdef CPPROMISE_USE_STACK_FREE
                    _dep_resolve(npm);
                    _dep_reject(npm);
#endif
                });
                case State::Fulfilled:
                return promise_t([this](promise_t &npm) {npm->_resolve(result);});
                case State::Rejected:
                return promise_t([this,
                                on_rejected = std::forward<FuncRejected>(on_rejected)
                                ](promise_t &npm) {
                    gen_on_rejected(std::move(on_rejected), npm)();
                });
                default: PROMISE_ERR_INVALID_STATE;
            }
        }
  
        void resolve() {
            if (state == State::Pending) trigger_fulfill();
        }

        void reject() {
            if (state == State::Pending) trigger_reject();
        }

        void resolve(pm_any_t _result) {
            if (state == State::Pending)
            {
                result = _result;
                trigger_fulfill();
            }
        }

        void reject(pm_any_t _reason) {
            if (state == State::Pending)
            {
                reason = _reason;
                trigger_reject();
            }
        }
    };
        
    template<typename PList> promise_t all(const PList &promise_list) {
        return promise_t([&promise_list] (promise_t &npm) {
            auto size = std::make_shared<size_t>(promise_list.size());
            auto results = std::make_shared<values_t>();
            if (!*size) PROMISE_ERR_MISMATCH_TYPE;
            results->resize(*size);
            size_t idx = 0;
            for (const auto &pm: promise_list) {
                pm->then(
                    [results, size, idx, npm](pm_any_t result) {
                        (*results)[idx] = result;
                        if (!--(*size))
                            npm->_resolve(*results);
                    },
                    [npm](pm_any_t reason) {npm->_reject(reason);});
#ifdef CPPROMISE_USE_STACK_FREE
                pm->_dep_resolve(npm);
                pm->_dep_reject(npm);
#endif
                idx++;
            }
        });
    }

    template<typename PList> promise_t race(const PList &promise_list) {
        return promise_t([&promise_list] (promise_t &npm) {
            for (const auto &pm: promise_list) {
                pm->then([npm](pm_any_t result) {npm->_resolve(result);},
                        [npm](pm_any_t reason) {npm->_reject(reason);});
#ifdef CPPROMISE_USE_STACK_FREE
                pm->_dep_resolve(npm);
                pm->_dep_reject(npm);
#endif
            }
        });
    }

    template<typename Func, disable_if_same_ref<Func, promise_t> *>
    inline promise_t::promise_t(Func &&callback):
            pm(new Promise()),
        ref_cnt(new size_t(1)) {
        callback(*this);
    }

    inline promise_t::promise_t():
        pm(new Promise()),
        ref_cnt(new size_t(1)) {}

    inline promise_t::~promise_t() {
        if (pm)
        {
            if (--*ref_cnt) return;
            delete pm;
            delete ref_cnt;
        }
    }

    template<typename T>
    inline void promise_t::resolve(T result) const { (*this)->resolve(result); }

    template<typename T>
    inline void promise_t::reject(T reason) const { (*this)->reject(reason); }

    inline void promise_t::resolve() const { (*this)->resolve(); }
    inline void promise_t::reject() const { (*this)->reject(); }

    template<typename T>
    struct callback_types {
        using arg_type = typename function_traits<T>::arg_type;
        using ret_type = typename std::conditional<
            std::is_same<typename function_traits<T>::ret_type, promise_t>::value,
            promise_t, pm_any_t>::type;
    };

    template<typename Func,
        disable_if_arg<Func, pm_any_t> * = nullptr,
        enable_if_return<Func, void> * = nullptr,
        typename function_traits<Func>::non_empty_arg * = nullptr>
    constexpr auto gen_any_callback(Func &&f) {
        using func_t = callback_types<Func>;
        return [f = std::forward<Func>(f)](pm_any_t v) mutable {
            try {
                f(any_cast<typename func_t::arg_type>(v));
            } catch (bad_any_cast &e) { PROMISE_ERR_MISMATCH_TYPE; }
        };
    }

    template<typename Func,
        enable_if_arg<Func, pm_any_t> * = nullptr,
        enable_if_return<Func, void> * = nullptr,
        typename function_traits<Func>::non_empty_arg * = nullptr>
    constexpr auto gen_any_callback(Func &&f) {
        return [f = std::forward<Func>(f)](pm_any_t v) mutable {f(v);};
    }

    template<typename Func,
        enable_if_return<Func, void> * = nullptr,
        typename function_traits<Func>::empty_arg * = nullptr>
    constexpr auto gen_any_callback(Func &&f) { return std::forward<Func>(f); }

    template<typename Func,
        enable_if_arg<Func, pm_any_t> * = nullptr,
        disable_if_return<Func, void> * = nullptr,
        typename function_traits<Func>::non_empty_arg * = nullptr>
    constexpr auto gen_any_callback(Func &&f) {
        using func_t = callback_types<Func>;
        return [f = std::forward<Func>(f)](pm_any_t v) mutable {
            return typename func_t::ret_type(f(v));
        };
    }

    template<typename Func,
        disable_if_arg<Func, pm_any_t> * = nullptr,
        disable_if_return<Func, void> * = nullptr,
        typename function_traits<Func>::non_empty_arg * = nullptr>
    constexpr auto gen_any_callback(Func &&f) {
        using func_t = callback_types<Func>;
        return [f = std::forward<Func>(f)](pm_any_t v) mutable {
            try {
                return typename func_t::ret_type(
                    f(any_cast<typename func_t::arg_type>(v)));
            } catch (bad_any_cast &e) { PROMISE_ERR_MISMATCH_TYPE; }
        };
    }

    template<typename Func,
        disable_if_return<Func, void> * = nullptr,
        typename function_traits<Func>::empty_arg * = nullptr>
    constexpr auto gen_any_callback(Func &&f) {
        using func_t = callback_types<Func>;
        return [f = std::forward<Func>(f)]() mutable {
            return typename func_t::ret_type(f());
        };
    }

    template<typename FuncFulfilled>
    inline promise_t promise_t::then(FuncFulfilled &&on_fulfilled) const {
        return (*this)->then(gen_any_callback(std::forward<FuncFulfilled>(on_fulfilled)));
    }

    template<typename FuncFulfilled, typename FuncRejected>
    inline promise_t promise_t::then(FuncFulfilled &&on_fulfilled,
                                    FuncRejected &&on_rejected) const {
        return (*this)->then(gen_any_callback(std::forward<FuncFulfilled>(on_fulfilled)),
                            gen_any_callback(std::forward<FuncRejected>(on_rejected)));
    }

    template<typename FuncRejected>
    inline promise_t promise_t::fail(FuncRejected &&on_rejected) const {
        return (*this)->fail(gen_any_callback(std::forward<FuncRejected>(on_rejected)));
    }
}

#endif
