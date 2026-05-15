#ifndef WELCOMEDIALOG_H
#define WELCOMEDIALOG_H

#include <QDialog>

class QPushButton;
class QListWidget;

class WelcomeDialog : public QDialog
{
    Q_OBJECT
    
public:
    enum Result {
        NewProject,
        OpenProject
    };
    
    explicit WelcomeDialog(QWidget *parent = nullptr);
    ~WelcomeDialog();
    
    Result userChoice() const;
    QString selectedRecentFile() const;

private slots:
    void onNewProject();
    void onOpenProject();

private:
    void setupUi();
    void loadRecentFiles();
    
    Result choice_;
    QString selectedRecentFile_;
    QListWidget *recentList_;
    QPushButton *newButton_;
    QPushButton *openButton_;
};

#endif // WELCOMEDIALOG_H
