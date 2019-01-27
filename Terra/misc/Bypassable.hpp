#pragma once

#include <atomic>

NS_HWM_BEGIN

//! Bypass状態へ移行する機能を提供するクラス
class BypassFlag final
{
public:
    //! コンストラクタ
    BypassFlag();
    
    //! デストラクタ
    /*! @pre すべてのBypassリクエストがReleaseされていること。
     *  (i.e., Bypass中にデストラクタが呼ばれてはならない）
     *  @pre 非ガード状態であること
     */
    ~BypassFlag();
    
    //! Bypass状態への移行が起こらないようにガードする。
    /*! すでにBypass状態へのリクエストが一つ以上存在している場合は、
     *  ガードに失敗し、falseが返る
     */
    bool BeginBypassGuard();
    
    //! Bypass状態への移行が起こらないようにするガードを解除
    void EndBypassGuard();
    
    //! Bypass状態への移行がガードされているかどうか
    bool IsBypassGuardEnabled() const;
    
    //! Bypass状態への移行をリクエストする
    /*! BeginBypassGuard()によるガードが行われていなければ、Bypass状態への移行し、trueを返す。
     *  BeginBypassGuard()によるガードが有効な時は、Bypass状態には移行せず、リクエストを投げた状態でfalseを返す。
     *  このとき、`WaitForEnableBypassing()`を呼び出すと、Bypass状態に移行するまで待機できる。
     *  @return Bypass状態が適用されたかどうか
     */
    bool RequestToBypass();
    
    //! RequestしたBypass状態への移行が適用されるまで待機する
    /*! RequestToBypass()の呼び出しでtrueが返り、
     *  すでにBypass状態への移行がすでに完了している場合は何もせず、即座に処理を返す。
     *  @pre 事前にRequestToBypass()を呼び出していること
     */
    void WaitToApplyBypassing();
    
    //! Bypassのリクエストを一つを解放する。
    /*! すべての複数のRequestToBypass()の呼び出しと同じ数だけReleaseBypassRequest()が呼び出されると、
     *  BypassFlagのBypass状態が解除される。
     */
    void ReleaseBypassRequest();
    
    //! RequestToBypass()の呼び出しすべてに対応するReleaseBypassRequestが呼び出されて、
    //! Bypass状態が解除されるのを待機する
    void WaitToFinishBypassing();
    
private:
    //! 最下位ビットで、BypassGuard状態かどうかを表す
    //! 2ビット目以降は、Bypass状態への移行リクエスト数を表す
    //! （つまり、`status >> 1`が、リクエスト数）
    std::atomic<std::uint32_t> status_;
};

//! Bypass状態への移行を、あるスコープ中だけガードするためのRAIIクラス
class ScopedBypassRequest
{
public:
    //! コンストラクタ
    /*! @post is_bypassing() == false
     */
    ScopedBypassRequest();
    
    //! コンストラクタ
    /*! コンストラクタの内部で、by.RequestToBypass()を呼び出し、それが成功した場合には
     *  デストラクタでReleaseBypassRequest()を呼び出す。
     *  @param should_wait
     *  Bypassがガードされているときに、そのガードが外れるまで待機するかどうか。
     *  これがtrueの場合は、コンストラクタから処理が戻った時点で、is_bypassing() == trueが保証される。
     */
    ScopedBypassRequest(BypassFlag &by, bool should_wait);
    
    //! デストラクタ
    /*! もしコンストラクタでのby.RequestToBypass()の呼び出しが成功していた場合は、
     *  ReleaseBypassRequest()を呼び出す。
     */
    ~ScopedBypassRequest();
    
    //! コンストラクタでのBeginBypassRequest()の呼び出しが成功して、バイパス状態になったかどうかをbool値で返す
    explicit operator bool() const noexcept;
    //! コンストラクタでのBeginBypassRequest()の呼び出しが成功して、バイパス状態になったかどうかをbool値で返す
    bool is_bypassing() const noexcept;
    
    ScopedBypassRequest(ScopedBypassRequest &&rhs);
    ScopedBypassRequest & operator=(ScopedBypassRequest &&rhs);
    
    //! is_bypassing() == trueの場合は、ReleaseBypassRequest()を呼び出して、デフォルトコンストラクト時と同じ状態にする。
    //! そうでない場合は何もしない。
    void reset();
    
private:
    ScopedBypassRequest(ScopedBypassRequest const &rhs) = delete;
    ScopedBypassRequest & operator=(ScopedBypassRequest const &rhs) = delete;
    
private:
    BypassFlag *by_;
};

[[nodiscard]] ScopedBypassRequest MakeScopedBypassRequest(BypassFlag &by, bool should_wait);

//! Bypass状態への移行を、あるスコープ中だけガードするためのRAIIクラス
class ScopedBypassGuard
{
public:
    //! コンストラクタ
    /*! @post is_guarded() == false
     */
    ScopedBypassGuard();
    //! コンストラクタ
    /*! コンストラクタの内部で、by.BeginBypassGuard()を呼び出し、それが成功した場合には
     *  デストラクタでEndBypassGuard()を呼び出す。
     *  @post byのBeginBypassGuard()が成功した場合に限り、is_guarded() == true
     *  @sa ~ScopedBypassGuard()
     */
    ScopedBypassGuard(BypassFlag &by);
    //! デストラクタ
    /*! もしコンストラクタでのby.BeginBypassGuard()の呼び出しが成功していた場合は、
     *  EndBypassGuard()を呼び出す。
     */
    ~ScopedBypassGuard();
    
    //! コンストラクタでのBeginBypassGuard()の呼び出しが成功したかどうかをbool値で返す
    explicit operator bool() const noexcept;
    //! コンストラクタでのBeginBypassGuard()の呼び出しが成功したかどうかをbool値で返す
    bool is_guarded() const noexcept;
    
    ScopedBypassGuard(ScopedBypassGuard &&rhs);
    ScopedBypassGuard & operator=(ScopedBypassGuard &&rhs);
    
    void reset();

private:
    ScopedBypassGuard(ScopedBypassGuard const &rhs) = delete;
    ScopedBypassGuard & operator=(ScopedBypassGuard const &rhs) = delete;
    
private:
    BypassFlag *by_;
};

[[nodiscard]] ScopedBypassGuard MakeScopedBypassGuard(BypassFlag &by);

NS_HWM_END
