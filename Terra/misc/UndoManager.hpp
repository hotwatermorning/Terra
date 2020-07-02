#pragma once

#include <memory>
#include <string>

#include "./SingleInstance.hpp"


NS_HWM_BEGIN

//! the base class for Undoable action.
class IUndoable
{
protected:
    IUndoable()
    {}

public:
    virtual ~IUndoable()
    {}

    //! This function is called when the action should perform.
    virtual
    void perform()
    {}

    //! This function is called when the action should undo.
    virtual
    void undo()
    {}

    //! This function is called when the action should redo.
    /*! The default behavior is just call perform().
     */
    virtual
    void redo() { perform(); }
};

using IUndoablePtr = std::unique_ptr<IUndoable>;

class UndoManager
:   public SingleInstance<UndoManager>
{
public:
    struct Transaction;

    //! Construtor
    UndoManager();

    //! Destructor
    /*! @pre `IsInTransaction() == false`
     *  @note All IUndoable object are deleted in
     *  the order which they were added to the transaction.
     */
    ~UndoManager();

    //! Get the number of performed transactions.
    /*! this also means the number of the undoable count.
     */
    int GetNumPerformedTransactionHistory() const noexcept;

    //! Get the number of undone transactions.
    /*! this also means the number of the redoable count.
     */
    int GetNumUndoneTransactionHistory() const noexcept;

    //! Get the number of history count limit.
    int GetNumHistoryLimit() const noexcept;

    //! Set the number of history count limit.
    /*! if `GetNumPerformedTransactionHistory() + GetNumUndoneTransactionHistory() > num`,
     *  older history will be deleted.
     */
    void SetNumHistoryLimit(int num);

    //! Begin undo transaction.
    /*! @param name the name of this transaction. This will be shown in undo menu command.
     */
    void BeginTransaction(String name);

    //! Transaction mode determines the execution order of undoing IUndoable objects in the transaction.
    enum class TransactionMode {
        //! The IUndoable objects in the transaction are executed in the same order of perform() execution.
        kUndoFromFront,
        //! The IUndoable objects in the transaction are executed in the reverse order of perform() execution.
        kUndoFromBack,
    };

    //! End undo transaction.
    /*! @param mode transaction mode
     */
    void EndTransaction(TransactionMode mode);

    //! Check whether an undo transaction has begun.
    bool IsInTransaction() const noexcept;

    //! Call perform() of p and add the object into the transaction.
    /*! if `CanRedo() == true`, then all redoable IUndoable object are deleted in
     *  the order which they were added to the transaction.
     *  @pre `IsInTransaction() == true`
     */
    void PerformAndAdd(std::unique_ptr<IUndoable> p);

    //! Get latest undo transaction's name.
    /*! @return returns the latest undo transaction name if undoable, std::nullopt otherwise.
     */
    std::optional<String> GetLatestUndoTransactionName() const;

    //! Get latest redo transaction's name.
    /*! @return returns the latest redo transaction name if redoable, std::nullopt otherwise.
    */
    std::optional<String> GetLatestRedoTransactionName() const;

    //! Get whether the undo manager can undo.
    bool IsUndoable() const noexcept;

    //! Get whether this undo manager can redo.
    bool IsRedoable() const noexcept;

    //! Undo a transaction.
    /*! @pre `IsUndoable() == true`
     */
    void Undo();

    //! Redo a transaction.
    /*! @pre `IsRedoable() == false`
     */
    void Redo();

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

//! Create an undo transaction which is valid only for a scope.
struct [[nodiscard]] ScopedUndoTransaction
{
    //! Create an undo transaction.
    /*! @param transaction_name the name of the transaction.
     *  @param mode the mode of the transaction.
     */
    ScopedUndoTransaction(String transaction_name,
                          UndoManager::TransactionMode mode = UndoManager::TransactionMode::kUndoFromBack);

    //! Get the assigned transaction mode.
    UndoManager::TransactionMode GetTransactionMode() const noexcept;

    //! Set a transaction mode.
    void SetTransactionMode(UndoManager::TransactionMode mode) noexcept;

    //! Destructor
    /*! Call `UndoManager::EndTransaction()` with the assigned transaction mode.
     */
    ~ScopedUndoTransaction();

private:
    UndoManager::TransactionMode mode_;
};

//! Create and perform a specified UndoableAction,
//! and add it into the current undo transaction.
/*! @tparam TUndoable The class derived from IUndoable.
 *  @param args The arguments which will be passed to the TUndoable's constructor.
 */
template<class TUndoable, class... Args>
void PerformAndAdd(Args &&... args)
{
    auto action = std::make_unique<TUndoable>(std::forward<Args>(args)...);

    auto um = UndoManager::GetInstance();
    um->PerformAndAdd(std::move(action));
}

NS_HWM_END
