#include <QByteArray>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QLockFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextStream>
#include <QVariant>
#include <QVector>

#define QT_DEBUG

class ZathuraManager {
    struct HistoryItem {
        HistoryItem() : page(0) {}
        explicit HistoryItem(uint32_t page) : page(page) {}

        uint32_t page;
    };

    using ParsedHistoryMap = QMap<QString /* path */, HistoryItem>;

   public:
    ZathuraManager(const QString &prefix)
        : orig_history_items_({}),
          added_history_items_({}),
          removed_history_items_({}),
          pos_(nullptr),
          path_(""),
          prefix_(prefix),
          orig_file_(QFile()),
          orig_file_lock_(nullptr) {}
    ~ZathuraManager() { close(); }

    bool open(const QString &path = "~/.local/share/zathura/history") {
        if (path.length() < 1) return false;
        path_ = path;
        if (path_[0] == '~') {
            path_.remove(0, 1);
            path_ = QString("%1/%2").arg(QDir::homePath(), path_);
        }

        orig_file_.setFileName(path_);
        if (!orig_file_.open(QIODevice::ReadWrite | QIODevice::Text)) {
            close();
            return false;
        }

        orig_file_lock_ = new QLockFile(path_ + ".lock");
        if (!orig_file_lock_->lock()) {
            close();
            return false;
        }

        readOrigFile();

        return true;
    }

    void close() {
        if (orig_file_.isOpen()) orig_file_.close();
        orig_file_.setFileName("");
        if (orig_file_lock_) {
            if (orig_file_lock_->isLocked()) orig_file_lock_->unlock();
            delete orig_file_lock_;
            orig_file_lock_ = nullptr;
        }
        orig_history_items_.clear();
        added_history_items_.clear();
        removed_history_items_.clear();
        pos_ = nullptr;
        path_ = "";
    }

    bool save() {
        if (!orig_file_.isOpen()) return false;

        if (added_history_items_.isEmpty() &&
            removed_history_items_.isEmpty()) {
            qInfo() << "No changes.";
            return false;
        }

        qInfo() << "Updated positions:";
        for (auto const &m : added_history_items_.toStdMap())
            qInfo() << m.first << " page:" << m.second.page;
        qInfo() << "";

        mergeHistoryItems();

        return writeOrigFile();
    }

    void updatePage(const QString &item_path, uint32_t page) {
        if (item_path.isEmpty()) return;

        ParsedHistoryMap::iterator found;

        QString path_with_prefix = QString("%1%2").arg(prefix_, item_path);

        found = added_history_items_.find(path_with_prefix);
        if (found != added_history_items_.end()) {
            added_history_items_.insert(path_with_prefix, HistoryItem{page});
            removed_history_items_.insert(path_with_prefix, HistoryItem{page});
            return;
        }

        found = orig_history_items_.find(path_with_prefix);
        if (found != orig_history_items_.end()) {
            added_history_items_.insert(path_with_prefix, HistoryItem{page});
            removed_history_items_.insert(path_with_prefix, HistoryItem{page});
        }
    }

   private:
    void mergeHistoryItems() {
        ParsedHistoryMap::const_iterator i;

        for (i = removed_history_items_.begin();
             i != removed_history_items_.end(); ++i)
            orig_history_items_.remove(i.key());
        for (i = added_history_items_.begin(); i != added_history_items_.end();
             ++i)
            orig_history_items_.insert(i.key(), i.value());

        added_history_items_.clear();
        removed_history_items_.clear();
    }

    void readOrigFile() {
        Q_ASSERT(orig_file_.isOpen());

        QTextStream in(&orig_file_);
        QString line;
        QVariant header;
        while (in.readLineInto(&line)) {
            if (!line.isEmpty() && !(line[0] == '#')) {
                if (line[0] == '[' && line[line.size() - 1] == ']' &&
                    line[1] == '/') {
                    line.remove(0, 1);
                    line.remove(line.size() - 1, 1);
                    header = {line};
                } else {
                    QRegExp page_rx("^page=(\\d+)$");
                    auto match = page_rx.exactMatch(line);
                    if (match) {
                        HistoryItem hi;
                        hi.page = page_rx.capturedTexts()[1].toInt();
                        if (!header.isNull())
                            orig_history_items_[header.toString()] = hi;
                        header = {};
                    }
                }
            }
        }
    }

    bool writeOrigFile() {
        Q_ASSERT(orig_file_.isOpen());
        Q_ASSERT(!path_.isEmpty());

        QFile temp_file(path_ + ".new");
        if (!temp_file.open(QFile::WriteOnly | QFile::Text)) return false;
        QTextStream out(&temp_file);

        QTextStream in(&orig_file_);
        in.seek(0);
        QString line;
        QVariant header;
        while (in.readLineInto(&line)) {
            if (!line.isEmpty() && !(line[0] == '#')) {
                if (line[0] == '[' && line[line.size() - 1] == ']' &&
                    line[1] == '/') {
                    QString line_copy = line;
                    line_copy.remove(0, 1);
                    line_copy.remove(line_copy.size() - 1, 1);
                    header = {line_copy};
                } else {
                    QRegExp page_rx("^page=(\\d+)$");
                    if (page_rx.exactMatch(line) && !header.isNull()) {
                        auto found =
                            orig_history_items_.find(header.toString());
                        if (found != orig_history_items_.end())
                            line = "page=" + QString::number(found->page);
                        header = {};
                    }
                }
            }
            out << line << "\n";
        }

        return true;
    }

   private:
    ParsedHistoryMap orig_history_items_;
    ParsedHistoryMap added_history_items_;
    ParsedHistoryMap removed_history_items_;

    QString::const_iterator pos_;
    QString path_;
    QString prefix_;  ///< Prefix to Documents directory on this device
    QFile orig_file_;
    QLockFile *orig_file_lock_;
};

struct BookInfo {
    BookInfo() : filename(""), hash_uuid(""), page(0) {}
    QString filename;
    QString filepath;  // Including path prefix "/mnt/ext{0-9}*/"
    QString hash_uuid;
    uint32_t page;
};

class PBDB final {
   public:
    PBDB(const QString &db_path)
        : path_(db_path), connection_(QSqlDatabase()), openned_(false) {}
    ~PBDB() { close(); }

    bool open() {
        connection_ = QSqlDatabase::addDatabase("QSQLITE", connection_name);
        connection_.setHostName("local");
        connection_.setDatabaseName(path_);
        return (openned_ = connection_.open());
    }

    bool fetchBookInfo(BookInfo &bi) {
        Q_ASSERT(openned_);
        QSqlQuery q(connection_);

        if (!q.prepare("SELECT OID FROM Items WHERE HashUUID = :hash;"))
            return false;
        q.bindValue(":hash", bi.hash_uuid.toUpper());
        if (!q.exec()) return false;
        if (!q.isActive() || !q.first()) return false;
        int item_id = q.value(0).toInt();

        if (!q.prepare(
                "SELECT PathID, Name FROM Files WHERE BookID = :item_id;"))
            return false;
        q.bindValue(":item_id", item_id);
        if (!q.exec()) return false;
        if (!q.isActive() || !q.first()) return false;
        int path_id = q.value(0).toInt();
        bi.filename = q.value(1).toString();

        if (!q.prepare("SELECT Path FROM Paths WHERE OID = :path_id;"))
            return false;
        q.bindValue(":path_id", path_id);
        if (!q.exec()) return false;
        if (!q.isActive() || !q.first()) return false;
        bi.filepath = q.value(0).toString();
        if (bi.filepath.isEmpty()) return false;

        return true;
    }

   private:
    void close() {
        if (!openned_) return;
        connection_.close();
        connection_ = QSqlDatabase();
        openned_ = false;
    }

   private:
    static constexpr const char *connection_name = "PBSync_conn";
    QString path_;
    QSqlDatabase connection_;
    bool openned_;
};

class PBManager final {
   public:
    PBManager(const QString &pb_mount_path)
        : path_(pb_mount_path), db_(PBDB(path_ + "system/config/books.db")) {}

    bool connect() {
        if (!db_.open()) return false;
        collectBookInfos();
        return true;
    }

    bool updateZathuraHistory(ZathuraManager &zm) {
        for (const auto &bi : book_infos_) {
            auto filepath_wo_prefix = bi.filepath;
            filepath_wo_prefix.remove(QRegExp("^/mnt/ext(\\d)+/"));
            auto full_path =
                QString("%1%2").arg(filepath_wo_prefix, bi.filename);
            zm.updatePage(full_path, bi.page);
        }
        return zm.save();
    }

   private:
    bool readPosition(QFile &f, BookInfo &bi) const {
        QRegExp text_rx("^text=pbr:/(word|page)\\?page=(\\d+).*$");
        if (!f.open(QFile::ReadOnly | QFile::Text)) return false;
        QTextStream ts(&f);
        QString data = ts.readAll();
        for (const QString &line : data.split('\n', Qt::SkipEmptyParts)) {
            auto match = text_rx.exactMatch(line);
            if (match) {
                bi.page = text_rx.capturedTexts()[2].toInt();
                return true;
            }
        }
        return false;
    }

    void collectBookInfos() {
        QDir dir(path_ + "system/state/cache/reader/");
        for (const QFileInfo &fi : dir.entryInfoList()) {
            if (!fi.isDir()) continue;
            BookInfo bi;
            bi.hash_uuid = fi.baseName();
            QFile pos_file(fi.absoluteFilePath() + "/position.cfg");
            if (!readPosition(pos_file, bi)) continue;
            if (!db_.fetchBookInfo(bi)) continue;
            book_infos_.push_back(bi);
        }
    }

   private:
    QVector<BookInfo> book_infos_;
    QString path_;
    PBDB db_;
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("pbsync");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "A CLI utility for syncing book positions between PocketBook e-reader "
        "and zathura");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        "mount-point",
        "PocketBook mount point: path with \"system\" directory.");
    parser.addPositionalArgument("zathura-history", "Zathura history file.");
    parser.addPositionalArgument("prefix",
                                 "Prefix to books location on this device.");

    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.size() != 3) {
        parser.showHelp();
        exit(EXIT_FAILURE);
    }

    QString mount_path(args[0]);

    auto pm = PBManager(mount_path);
    if (!pm.connect()) exit(EXIT_FAILURE);

    QString zathura_history_path = args[1];
    QString prefix = args[2];
    auto zm = ZathuraManager(prefix);
    if (!zm.open(zathura_history_path)) exit(EXIT_FAILURE);

    if (!pm.updateZathuraHistory(zm)) exit(EXIT_FAILURE);

    qInfo()
        << "New zathura history was saved at"
        << QString("%1.new").arg(zathura_history_path).toStdString().c_str();
    qInfo() << "";
    qInfo() << "Check the difference using:";
    qInfo()
        << " diff"
        << QString("%1{,.new}").arg(zathura_history_path).toStdString().c_str();
    qInfo() << "";
    qInfo() << "To apply these changes use: ";
    qInfo()
        << " cp"
        << QString("%1{,.new}").arg(zathura_history_path).toStdString().c_str();

    return 0;
}
