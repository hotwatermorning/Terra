#include "UndoManager.hpp"
#include <stack>

NS_HWM_BEGIN

using Transaction = UndoManager::Transaction;
using TransactionPtr = std::unique_ptr<Transaction>;

//! The transaction of A container of IUndoable objects
struct UndoManager::Transaction
{
    //! A class holding a TransactionPtr or an IUndoablePtr.
    /*! This class abstracts Transaction and IUndable.
     */
    struct Action {
        Action(TransactionPtr t) : t_(std::move(t)) {}
        Action(IUndoablePtr u) : u_(std::move(u)) {}

        TransactionPtr t_;
        IUndoablePtr u_;

        void perform() { t_ ? t_->perform() : u_->perform(); }
        void undo() { t_ ? t_->undo() : u_->undo(); }
        void redo() { t_ ? t_->redo() : u_->redo(); }
        void reset() { t_ ? t_->reset() : u_.reset(); }
    };

    String name_;

    //! The undoable actions.
    /*! When perform() or redo(), these actions are executed from front to back.
     *  When undo(), the execution order depends on transaction_mode_.
     */
    std::vector<Action> actions_;
    TransactionMode transaction_mode_ = TransactionMode::kUndoFromBack;

    //! A flag represents this transaction has been undone.
    bool undone_ = false;

    //! Constructor
    Transaction()
    {}

    Transaction(Transaction const &rhs) = delete;
    Transaction & operator=(Transaction const &rhs) = delete;
    Transaction(Transaction &&rhs) = default;
    Transaction & operator=(Transaction &&rhs) = default;

    //! Destructor
    ~Transaction()
    {
        reset();
    }

    //! Perform actions.
    void perform()
    {
        std::for_each(actions_.begin(), actions_.end(), [](auto &action) { action.perform(); });
    }

    //! Undo actions.
    void undo()
    {
        auto undo_impl = [](auto first, auto last) {
            std::for_each(first, last, [](auto &action) { action.undo(); });
        };

        if(transaction_mode_ == TransactionMode::kUndoFromFront) {
            undo_impl(actions_.begin(), actions_.end());
        } else {
            undo_impl(actions_.rbegin(), actions_.rend());
        }

        undone_ = true;
    }

    //! Redo actions.
    void redo()
    {
        std::for_each(actions_.begin(), actions_.end(), [](auto &action) { action.redo(); });
        undone_ = false;
    }

    //! Reset this transaction.
    void reset()
    {
        std::for_each(actions_.begin(), actions_.end(), [](auto &action) { action.reset(); });
        actions_.clear();
    }
};

struct UndoManager::Impl
{
    Impl()
    {}

    int num_history_limit_ = 1000;

    //! Performed or redone transactions.
    std::vector<TransactionPtr> performed_;

    //! Undone transactions.
    std::vector<TransactionPtr> undone_;

    //! The stack
    std::stack<TransactionPtr> transaction_stack_;
};

UndoManager::UndoManager()
:   pimpl_(std::make_unique<Impl>())
{

}

UndoManager::~UndoManager()
{
    assert(IsInTransaction() == false);
}

int UndoManager::GetNumPerformedTransactionHistory() const noexcept
{
    return pimpl_->performed_.size();
}

int UndoManager::GetNumUndoneTransactionHistory() const noexcept
{
    return pimpl_->undone_.size();
}

int UndoManager::GetNumHistoryLimit() const noexcept
{
    return pimpl_->num_history_limit_;
}

void UndoManager::SetNumHistoryLimit(int num)
{
    auto const saved_num = num;

    int const num_to_remove_performed = std::max<int>(GetNumPerformedTransactionHistory(), num) - num;
    for(int i = 0; i < num_to_remove_performed; ++i) {
        pimpl_->performed_[i].reset();
    }
    pimpl_->performed_.erase(pimpl_->performed_.begin(),
                             pimpl_->performed_.begin() + num_to_remove_performed
                             );

    num -= num_to_remove_performed;

    int const num_to_remove_undone = std::max<int>(GetNumUndoneTransactionHistory(), num) - num;
    for(int i = 0; i < num_to_remove_undone; ++i) {
        pimpl_->undone_[pimpl_->undone_.size() - i - 1].reset();
    }

    pimpl_->performed_.erase(pimpl_->performed_.end() - num_to_remove_undone - 1,
                             pimpl_->performed_.end()
                             );

    pimpl_->num_history_limit_ = saved_num;
}

void UndoManager::BeginTransaction(String name)
{
    auto t = std::make_unique<Transaction>();
    t->name_ = name;

    pimpl_->transaction_stack_.push(std::move(t));
}

void UndoManager::EndTransaction(UndoManager::TransactionMode mode)
{
    assert(IsInTransaction());

    auto t = std::move(pimpl_->transaction_stack_.top());
    pimpl_->transaction_stack_.pop();

    //! if no actions are added, don't record the transaction.
    if(t->actions_.empty()) { return; }

    t->transaction_mode_ = mode;

    if(IsInTransaction()) {
        auto &outer_t = pimpl_->transaction_stack_.top();
        outer_t->actions_.push_back(std::move(t));
        return;
    } else {
        pimpl_->performed_.push_back(std::move(t));

        while(pimpl_->undone_.size() > 0) {
            pimpl_->undone_.pop_back();
        }
    }
}

bool UndoManager::IsInTransaction() const noexcept
{
    return pimpl_->transaction_stack_.empty() == false;
}

void UndoManager::PerformAndAdd(std::unique_ptr<IUndoable> p)
{
    assert(IsInTransaction());

    p->perform();
    auto &t = pimpl_->transaction_stack_.top();
    t->actions_.push_back(std::move(p));
}

std::optional<String> UndoManager::GetLatestUndoTransactionName() const
{
    if(pimpl_->performed_.size() > 0) {
        return pimpl_->performed_.back()->name_;
    } else {
        return std::nullopt;
    }
}

std::optional<String> UndoManager::GetLatestRedoTransactionName() const
{
    if(pimpl_->undone_.size() > 0) {
        return pimpl_->undone_.back()->name_;
    } else {
        return std::nullopt;
    }
}

bool UndoManager::IsUndoable() const noexcept
{
    return GetNumPerformedTransactionHistory() > 0 && IsInTransaction() == false;
}

bool UndoManager::IsRedoable() const noexcept
{
    return GetNumUndoneTransactionHistory() > 0 && IsInTransaction() == false;
}

void UndoManager::Undo()
{
    assert(IsUndoable());

    auto action = std::move(pimpl_->performed_.back());
    pimpl_->performed_.pop_back();
    action->undo();
    pimpl_->undone_.push_back(std::move(action));
}

void UndoManager::Redo()
{
    assert(IsRedoable());

    auto action = std::move(pimpl_->undone_.back());
    pimpl_->undone_.pop_back();
    action->redo();
    pimpl_->performed_.push_back(std::move(action));
}

ScopedUndoTransaction::ScopedUndoTransaction(String transaction_name,
                                             UndoManager::TransactionMode mode)
:   mode_(mode)
{
    auto um = UndoManager::GetInstance();
    assert(um);

    um->BeginTransaction(transaction_name);
}

UndoManager::TransactionMode
ScopedUndoTransaction::GetTransactionMode() const noexcept
{
    return mode_;
}

void
ScopedUndoTransaction::SetTransactionMode(UndoManager::TransactionMode mode) noexcept
{
    mode_ = mode;
}

ScopedUndoTransaction::~ScopedUndoTransaction()
{
    auto um = UndoManager::GetInstance();
    assert(um);

    try {
        um->EndTransaction(mode_);
    } catch(std::exception &e) {
        assert(false && e.what());
    }
}

NS_HWM_END
