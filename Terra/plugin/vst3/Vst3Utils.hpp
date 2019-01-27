#pragma once

#include <memory>
#include <utility>

#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/vstspeaker.h>

#include "../../misc/StrCnv.hpp"
#include "../../misc/Either.hpp"

NS_HWM_BEGIN

class SelfReleaser
{
public:
    void operator() (Steinberg::FUnknown *p) {
		if(p) {
			p->release();
		}
	}
};

template<class T>
using vstma_unique_ptr = std::unique_ptr<T, SelfReleaser>;

//! Make an FUnknown pointer auto-releasable.
template<class T>
vstma_unique_ptr<T>  to_unique(T *p)
{
	return vstma_unique_ptr<T>(p);
}

template<class To>
using maybe_vstma_unique_ptr = Either<Steinberg::tresult, vstma_unique_ptr<To>>;

//! pに対してqueryInterfaceを呼び出し、その結果を返す。
/*!
	@return queryInterfaceが正常に完了し、有効なポインタが返ってきた場合は、
	Rightのオブジェクトが設定されたEitherが返る。
	queryInterfaceがkResultTrue以外を返して失敗した場合は、そのエラーコードをLeftに設定する。
	queryInterfaceによって取得されたポインタがnullptrだった場合は、kNoInterfaceをLeftに設定する。。
	@note 	失敗した時に、そのエラーコードが必要になることを考えて、Boost.Optionalではなく、Eitherを返すようにした
*/
template<class To, class T>
maybe_vstma_unique_ptr<To> queryInterface_impl(T *p, Steinberg::FIDString iid)
{
    assert(p);

	To *obtained = nullptr;
	Steinberg::tresult const res = p->queryInterface(iid, (void **)&obtained);
	if(res == Steinberg::kResultTrue && obtained) {
        return { to_unique(obtained) };
	} else {
		if(res != Steinberg::kResultTrue) {
            return { res };
		} else {
            return { Steinberg::kNoInterface };
		}
	}
}

// もしPointerの名前空間内に、別のget_raw_pointerが定義されていても、こちらの関数が必ず使用されるようにする。
namespace prevent_adl {

	template<class T>
	auto get_raw_pointer(T *p) -> T * { return p; }

	template<class T>
	auto get_raw_pointer(T const &p) -> decltype(p.get()) { return p.get(); }

}	// prevent_adl

template<class To, class Pointer>
maybe_vstma_unique_ptr<To> queryInterface(Pointer const &p, Steinberg::FIDString iid)
{
	return queryInterface_impl<To>(prevent_adl::get_raw_pointer(p), iid);
}

template<class To, class Pointer>
maybe_vstma_unique_ptr<To> queryInterface(Pointer const &p)
{
	return queryInterface_impl<To>(prevent_adl::get_raw_pointer(p), To::iid);
}

template<class To, class Factory>
maybe_vstma_unique_ptr<To> createInstance_impl(Factory *factory, Steinberg::FUID class_id, Steinberg::FIDString iid)
{
    assert(factory);
	To *obtained = nullptr;

	Steinberg::tresult const res = factory->createInstance(class_id, iid, (void **)&obtained);
	if(res == Steinberg::kResultTrue && obtained) {
        return { to_unique(obtained) };
	} else {
        return { res };
	}
}

//! なんらかのファクトリクラスからあるコンポーネントを取得する。
template<class To, class FactoryPointer>
maybe_vstma_unique_ptr<To> createInstance(FactoryPointer const &factory, Steinberg::FUID class_id, Steinberg::FIDString iid)
{
	return createInstance_impl<To>(prevent_adl::get_raw_pointer(factory), class_id, iid);
}

//! なんらかのファクトリクラスからあるコンポーネントを取得する。
template<class To, class FactoryPointer>
maybe_vstma_unique_ptr<To> createInstance(FactoryPointer const &factory, Steinberg::FUID class_id)
{
	return createInstance_impl<To>(prevent_adl::get_raw_pointer(factory), class_id, To::iid);
}

inline
String GetSpeakerName(Steinberg::Vst::SpeakerArrangement arr, bool with_speaker_name = true)
{
    return to_wstr(Steinberg::Vst::SpeakerArr::getSpeakerArrangementString(arr, with_speaker_name));
}

NS_HWM_END
