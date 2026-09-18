#pragma once
namespace host {
// Worker-side ownership is authoritative; page enablement is only its projection.
class OperationCoordinator {
public:
    enum class Task { None, Download, Diagnostic, Scan, Signal };
    bool acquire(Task task){if(m_owner!=Task::None&&m_owner!=task)return false;m_owner=task;return true;}
    void release(Task task){if(m_owner==task)m_owner=Task::None;}
    Task owner()const{return m_owner;}
    bool permits(Task task)const{return m_owner==Task::None||m_owner==task;}
private: Task m_owner=Task::None;
};
}
