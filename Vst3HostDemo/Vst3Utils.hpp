#pragma once

#include <memory>
#include <Windows.h>

#include "./vst3/pluginterfaces/base/ftypes.h"
#include "./vst3/pluginterfaces/base/ipluginbase.h"

namespace hwm {

struct ModuleReleaser
{
	template<class Handle>
	void operator() (Handle handle) {
		if(handle) {
			FreeLibrary(handle);
		}
	}
};

struct SelfReleaser
{
	template<class T>
	void operator() (T *p) {
		if(p) {
			p->release();
		}
	}
};

typedef std::unique_ptr<std::remove_pointer<HMODULE>::type, ModuleReleaser> module_holder;
typedef std::unique_ptr<Steinberg::IPluginFactory, SelfReleaser> factory_ptr;

template<class T>
std::unique_ptr<T, SelfReleaser>  to_unique(T *p)
{
	return std::unique_ptr<T, SelfReleaser>(p);
}

//! 失敗か成功かどちらかの状況を返すクラス
//! is_right() == trueの時は成功の状況
template<class Left, class Right>
struct Either
{
	Either(Left left) : left_(std::move(left)), right_(Right()), is_right_(false) {}
	Either(Right right) : left_(Left()), right_(std::move(right)), is_right_(true) {}

	Either(Either &&rhs)
		:	left_(std::move(rhs.left_))
		,	right_(std::move(rhs.right_))
		,	is_right_(std::move(rhs.is_right_))
	{}

	Either & operator=(Either &&rhs)
	{
		left_ = std::move(rhs.left_);
		right_ = std::move(rhs.right_);
		is_right_ = std::move(rhs.is_right_);

		return *this;
	}

	bool is_right() const { return is_right_; }

	Left &			left	()
	{
		BOOST_ASSERT(!is_right());
		return left_;
	}

	Left const &	left	() const
	{
		BOOST_ASSERT(!is_right());
		return left_;
	}

	Right &			right	()
	{
		BOOST_ASSERT(is_right());
		return right_;
	}

	Right const &	right	() const
	{
		BOOST_ASSERT(is_right());
		return right_;
	}

	template<class F>
	void visit(F f)
	{
		if(is_right()) {
			visit_right(f);
		} else {
			visit_left(f);
		}
	}

	template<class F>
	void visit_left(F f) {
		if(!is_right()) { f(left_); }
	}

	template<class F>
	void visit_left(F f) const {
		if(!is_right()) { f(left_); }
	}

	template<class F>
	void visit_right(F f) {
		if(is_right()) { f(right_); }
	}

	template<class F>
	void visit_right(F f) const {
		if(is_right()) { f(right_); }
	}

private:
	bool is_right_;
	Left left_;
	Right right_;
};

//! pに対してqueryInterfaceを呼び出し、その結果を返す。
/*!
	@return queryInterfaceが正常に完了し、有効なポインタが返ってきた場合は、
	Rightのオブジェクトが設定されたEitherが返る。
	queryInterfaceがkResultTrue以外を返して失敗した場合は、そのエラーコードをLeftに設定する。
	queryInterfaceによって取得されたポインタがnullptrだった場合は、kNoInterfaceをLeftに設定する。。
	@note 	失敗した時に、そのエラーコードが必要になることを考えて、Boost.Optionalではなく、Eitherを返すようにした
*/
template<class To, class T>
Either<Steinberg::tresult, std::unique_ptr<To, SelfReleaser>> queryInterface_impl(T *p, Steinberg::FIDString iid)
{
	typedef Either<Steinberg::tresult, std::unique_ptr<To, SelfReleaser>> either_t;
	To *obtained = nullptr;
	Steinberg::tresult const res = p->queryInterface(iid, (void **)&obtained);
	if(res == kResultTrue && obtained) {
		return either_t(to_unique(obtained));
	} else {
		if(res != kResultTrue) {
			return either_t(res);
		} else {
			return kNoInterface;
		}
	}
}

namespace prevent_adl {

	template<class T>
	auto get_raw_pointer(T *p) -> T * { return p; }

	template<class T>
	auto get_raw_pointer(T const &p) -> decltype(p.get()) { return p.get(); }

}	// prevent_adl

template<class To, class Pointer>
Either<Steinberg::tresult, std::unique_ptr<To, SelfReleaser>> queryInterface(Pointer const &p, Steinberg::FIDString iid)
{
	return queryInterface_impl<To>(prevent_adl::get_raw_pointer(p), iid);
}

template<class To, class Pointer>
Either<Steinberg::tresult, std::unique_ptr<To, SelfReleaser>> queryInterface(Pointer const &p)
{
	return queryInterface_impl<To>(prevent_adl::get_raw_pointer(p), To::iid);
}

template<class To, class Factory>
Either<Steinberg::tresult, std::unique_ptr<To, SelfReleaser>> createInstance_impl(Factory *factory, Steinberg::FUID class_id, Steinberg::FIDString iid)
{
	typedef Either<Steinberg::tresult, std::unique_ptr<To, SelfReleaser>> either_t;
	To *obtained = nullptr;

	Steinberg::tresult const res = factory->createInstance(class_id, iid, (void **)&obtained);
	if(res == kResultTrue && obtained) {
		return either_t(to_unique(obtained));
	} else {
		return either_t(res);
	}
}

//! なんらかのファクトリクラスからあるコンポーネントを取得する。
template<class To, class FactoryPointer>
Either<Steinberg::tresult, std::unique_ptr<To, SelfReleaser>> createInstance(FactoryPointer const &factory, Steinberg::FUID class_id, Steinberg::FIDString iid)
{
	return createInstance_impl<To>(prevent_adl::get_raw_pointer(factory), class_id, iid);
}

//! なんらかのファクトリクラスからあるコンポーネントを取得する。
template<class To, class FactoryPointer>
Either<Steinberg::tresult, std::unique_ptr<To, SelfReleaser>> createInstance(FactoryPointer const &factory, Steinberg::FUID class_id)
{
	return createInstance_impl<To>(prevent_adl::get_raw_pointer(factory), class_id, To::iid);
}

}	// ::hwm