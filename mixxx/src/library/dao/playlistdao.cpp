#include <QtDebug>
#include <QtCore>
#include <QtSql>
#include "trackinfoobject.h"
#include "library/dao/playlistdao.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"

PlaylistDAO::PlaylistDAO(QSqlDatabase& database)
        : m_database(database) {
}

PlaylistDAO::~PlaylistDAO()
{
}

void PlaylistDAO::initialize()
{
}

int PlaylistDAO::createPlaylist(QString name, HiddenType hidden)
{
    // qDebug() << "PlaylistDAO::createPlaylist"
    //          << QThread::currentThread()
    //          << m_database.connectionName();
    //Start the transaction
    m_database.transaction();

    //Find out the highest position for the existing playlists so we know what
    //position this playlist should have.
    QSqlQuery query(m_database);
    query.prepare("SELECT max(position) as posmax FROM Playlists");

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        m_database.rollback();
        return -1;
    }

    //Get the id of the last playlist.
    int position = 0;
    if (query.next()) {
        position = query.value(query.record().indexOf("posmax")).toInt();
        position++; //Append after the last playlist.
    }

    //qDebug() << "Inserting playlist" << name << "at position" << position;

    query.prepare("INSERT INTO Playlists (name, position, hidden, date_created, date_modified) "
                  "VALUES (:name, :position, :hidden,  CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
    query.bindValue(":name", name);
    query.bindValue(":position", position);
    query.bindValue(":hidden", static_cast<int>(hidden));

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        m_database.rollback();
        return -1;
    }

    int playlistId = query.lastInsertId().toInt();
    //Commit the transaction
    m_database.commit();
    emit(added(playlistId));
    return playlistId;
}

QString PlaylistDAO::getPlaylistName(int playlistId)
{
    // qDebug() << "PlaylistDAO::getPlaylistName" << QThread::currentThread() << m_database.connectionName();

    QSqlQuery query(m_database);
    query.prepare("SELECT name FROM Playlists "
                  "WHERE id= :id");
    query.bindValue(":id", playlistId);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return "";
    }

    // Get the name field
    QString name = "";
    while (query.next()) {
        name = query.value(query.record().indexOf("name")).toString();
    }
    return name;
}

int PlaylistDAO::getPlaylistIdFromName(QString name) {
    // qDebug() << "PlaylistDAO::getPlaylistIdFromName" << QThread::currentThread() << m_database.connectionName();

    QSqlQuery query(m_database);
    query.prepare("SELECT id FROM Playlists WHERE name = :name");
    query.bindValue(":name", name);
    if (query.exec()) {
        if (query.next()) {
            return query.value(query.record().indexOf("id")).toInt();
        }
    } else {
        LOG_FAILED_QUERY(query);
    }
    return -1;
}

void PlaylistDAO::deletePlaylist(int playlistId)
{
    // qDebug() << "PlaylistDAO::deletePlaylist" << QThread::currentThread() << m_database.connectionName();
    m_database.transaction();

    //Get the playlist id for this
    QSqlQuery query(m_database);

    //Delete the row in the Playlists table.
    query.prepare("DELETE FROM Playlists "
                  "WHERE id= :id");
    query.bindValue(":id", playlistId);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        m_database.rollback();
        return;
    }

    //Delete the tracks in this playlist from the PlaylistTracks table.
    query.prepare("DELETE FROM PlaylistTracks "
                  "WHERE playlist_id = :id");
    query.bindValue(":id", playlistId);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        m_database.rollback();
        return;
    }

    m_database.commit();
    //TODO: Crap, we need to shuffle the positions of all the playlists?

    emit(deleted(playlistId));
}

void PlaylistDAO::renamePlaylist(int playlistId, const QString& newName) {
    QSqlQuery query(m_database);
    query.prepare("UPDATE Playlists SET name = :name WHERE id = :id");
    query.bindValue(":name", newName);
    query.bindValue(":id", playlistId);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }
    emit(renamed(playlistId));
}

bool PlaylistDAO::setPlaylistLocked(int playlistId, bool locked) {
    QSqlQuery query(m_database);
    query.prepare("UPDATE Playlists SET locked = :lock WHERE id = :id");
    // SQLite3 doesn't support boolean value. Using integer instead.
    query.bindValue(":lock", static_cast<int>(locked));
    query.bindValue(":id", playlistId);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    emit(lockChanged(playlistId));
    return true;
}

bool PlaylistDAO::isPlaylistLocked(int playlistId) {
    QSqlQuery query(m_database);
    query.prepare("SELECT locked FROM Playlists WHERE id = :id");
    query.bindValue(":id", playlistId);

    if (query.exec()) {
        if (query.next()) {
            int lockValue = query.value(0).toInt();
            return lockValue == 1;
        }
    } else {
        LOG_FAILED_QUERY(query);
    }
    return false;
}

void PlaylistDAO::appendTracksToPlaylist(QList<int> trackIds, int playlistId) {
    // qDebug() << "PlaylistDAO::appendTracksToPlaylist"
    //          << QThread::currentThread() << m_database.connectionName();

    // Start the transaction
    m_database.transaction();

    //Find out the highest position existing in the playlist so we know what
    //position this track should have.
    QSqlQuery query(m_database);
    query.prepare("SELECT max(position) as position FROM PlaylistTracks "
                  "WHERE playlist_id = :id");
    query.bindValue(":id", playlistId);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }

    // Get the position of the highest track in the playlist.
    int position = 0;
    if (query.next()) {
        position = query.value(query.record().indexOf("position")).toInt();
    }
    // Append after the last song. If no songs or a failed query then 0 becomes
    // 1.
    position++;

    //Insert the song into the PlaylistTracks table
    query.prepare("INSERT INTO PlaylistTracks (playlist_id, track_id, position, pl_datetime_added)"
                  "VALUES (:playlist_id, :track_id, :position, CURRENT_TIMESTAMP)");
    query.bindValue(":playlist_id", playlistId);


    foreach (int trackId, trackIds) {
        query.bindValue(":track_id", trackId);
        query.bindValue(":position", position++);
        if (!query.exec()) {
            LOG_FAILED_QUERY(query);
        }
    }

    // Commit the transaction
    m_database.commit();

    foreach (int trackId, trackIds) {
        emit(trackAdded(playlistId, trackId, position));
    }
    emit(changed(playlistId));
}

void PlaylistDAO::appendTrackToPlaylist(int trackId, int playlistId) {
    QList<int> tracks;
    tracks.append(trackId);
    appendTracksToPlaylist(tracks, playlistId);
}

unsigned int PlaylistDAO::playlistCount()
{
    // qDebug() << "PlaylistDAO::playlistCount" << QThread::currentThread() << m_database.connectionName();
    QSqlQuery query(m_database);
    query.prepare("SELECT count(*) as count FROM Playlists");
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }

    int numRecords = 0;
    if (query.next()) {
        numRecords = query.value(query.record().indexOf("count")).toInt();
    }
    return numRecords;
}

int PlaylistDAO::getPlaylistId(int index)
{
    // qDebug() << "PlaylistDAO::getPlaylistId"
    //          << QThread::currentThread() << m_database.connectionName();

    QSqlQuery query(m_database);
    query.prepare("SELECT id FROM Playlists");

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return -1;
    }

    int currentRow = 0;
    while(query.next()) {
        if (currentRow++ == index) {
            int id = query.value(0).toInt();
            return id;
        }
    }
    return -1;
}

PlaylistDAO::HiddenType PlaylistDAO::getHiddenType(int playlistId) {
    // qDebug() << "PlaylistDAO::getHiddenType"
    //          << QThread::currentThread() << m_database.connectionName();

    QSqlQuery query(m_database);
    query.prepare("SELECT hidden FROM Playlists WHERE id = :id");
    query.bindValue(":id", playlistId);

    if (query.exec()) {
        if (query.next()) {
            return static_cast<HiddenType>(query.value(0).toInt());
        }
    } else {
        LOG_FAILED_QUERY(query);
    }
    qDebug() << "PlaylistDAO::getHiddenType returns PLHT_UNKNOWN for playlistId "
             << playlistId;
    return PLHT_UNKNOWN;
}

bool PlaylistDAO::isHidden(int playlistId) {
    // qDebug() << "PlaylistDAO::isHidden"
    //          << QThread::currentThread() << m_database.connectionName();

    HiddenType ht = getHiddenType(playlistId);
    if (ht == PLHT_NOT_HIDDEN) {
        return false;
    }
    return true;
}

void PlaylistDAO::removeTrackFromPlaylists(int trackId) {
    QSqlQuery query(m_database);
    QString queryString = QString("SELECT %1, %2 FROM %3 ORDER BY %2 DESC")
            .arg(PLAYLISTTRACKSTABLE_PLAYLISTID,
                 PLAYLISTTRACKSTABLE_POSITION,
                 PLAYLIST_TRACKS_TABLE);
    query.prepare(queryString);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }

    int positionIndex = query.record().indexOf(PLAYLISTTRACKSTABLE_POSITION);
    int playlistIdIndex = query.record().indexOf(
        PLAYLISTTRACKSTABLE_PLAYLISTID);
    while (query.next()) {
        int position = query.value(positionIndex).toInt();
        int playlistId = query.value(playlistIdIndex).toInt();
        removeTrackFromPlaylist(playlistId, position);
    }
}

void PlaylistDAO::removeTrackFromPlaylist(int playlistId, int position)
{
    // qDebug() << "PlaylistDAO::removeTrackFromPlaylist"
    //          << QThread::currentThread() << m_database.connectionName();
    m_database.transaction();
    QSqlQuery query(m_database);

    query.prepare("SELECT id FROM PlaylistTracks WHERE playlist_id=:id "
                  "AND position=:position");
    query.bindValue(":id", playlistId);
    query.bindValue(":position", position);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        m_database.rollback();
        return;
    }

    if (!query.next()) {
        qDebug() << "removeTrackFromPlaylist no track exists at position:"
                 << position << "in playlist:" << playlistId;
        return;
    }
    int trackId = query.value(query.record().indexOf("id")).toInt();

    //Delete the track from the playlist.
    query.prepare("DELETE FROM PlaylistTracks "
                  "WHERE playlist_id=:id AND position= :position");
    query.bindValue(":id", playlistId);
    query.bindValue(":position", position);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        m_database.rollback();
        return;
    }

    QString queryString;
    queryString = QString("UPDATE PlaylistTracks SET position=position-1 "
                          "WHERE position>=%1 AND "
                          "playlist_id=%2").arg(QString::number(position),
                                                QString::number(playlistId));
    if (!query.exec(queryString)) {
        LOG_FAILED_QUERY(query);
    }
    m_database.commit();

    emit(trackRemoved(playlistId, trackId, position));
    emit(changed(playlistId));
}

void PlaylistDAO::insertTrackIntoPlaylist(int trackId, int playlistId, int position)
{
    if (playlistId < 0 || trackId < 0 || position < 0)
        return;

    m_database.transaction();

    // Move all the tracks in the playlist up by one
    QString queryString =
            QString("UPDATE PlaylistTracks SET position=position+1 "
                    "WHERE position>=%1 AND "
                    "playlist_id=%2").arg(QString::number(position),
                                          QString::number(playlistId));

    QSqlQuery query(m_database);
    if (!query.exec(queryString)) {
        LOG_FAILED_QUERY(query);
    }

    //Insert the song into the PlaylistTracks table
    query.prepare("INSERT INTO PlaylistTracks (playlist_id, track_id, position, pl_datetime_added)"
                  "VALUES (:playlist_id, :track_id, :position, CURRENT_TIMESTAMP)");
    query.bindValue(":playlist_id", playlistId);
    query.bindValue(":track_id", trackId);
    query.bindValue(":position", position);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }
    m_database.commit();

    emit(trackAdded(playlistId, trackId, position));
    emit(changed(playlistId));
}

void PlaylistDAO::addToAutoDJQueue(int playlistId, bool bTop) {
    //qDebug() << "Adding tracks from playlist " << playlistId << " to the Auto-DJ Queue";

    // Query the PlaylistTracks database to locate tracks in the selected playlist
    QSqlQuery query(m_database);
    query.prepare("SELECT track_id FROM PlaylistTracks "
                  "WHERE playlist_id = :plid");
    query.bindValue(":plid", playlistId);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }

    // Get the ID of the Auto-DJ playlist
    int autoDJId = getPlaylistIdFromName(AUTODJ_TABLE);

    // Loop through the tracks, adding them to the Auto-DJ Queue. Start at
    // position 2 because position 1 was already loaded to the deck
    int i = 2;

    while (query.next()) {
        if (bTop) {
            insertTrackIntoPlaylist(query.value(0).toInt(), autoDJId, i++);
        }
        else {
            appendTrackToPlaylist(query.value(0).toInt(), autoDJId);
        }
    }
}

int PlaylistDAO::getPreviousPlaylist(int currentPlaylistId, HiddenType hidden) {
    // Find out the highest position existing in the playlist so we know what
    // position this track should have.
    QSqlQuery query(m_database);
    query.prepare("SELECT max(id) as id FROM Playlists "
                  "WHERE id < :id AND hidden = :hidden");
    query.bindValue(":id", currentPlaylistId);
    query.bindValue(":hidden", hidden);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return -1;
    }

     // Get the id of the highest playlist
    int previousPlaylistId = -1;
    if (query.next()) {
        previousPlaylistId = query.value(query.record().indexOf("id")).toInt();
    }
    return previousPlaylistId;
}

void PlaylistDAO::copyPlaylistTracks(int sourcePlaylistID, int targetPlaylistId) {
    // Query Tracks from the source Playlist
    QSqlQuery query(m_database);
    query.prepare("SELECT track_id FROM PlaylistTracks "
                  "WHERE playlist_id = :plid");
    query.bindValue(":plid", sourcePlaylistID);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }

    QList<int> trackIds;
    while (query.next()) {
        trackIds.append(query.value(0).toInt());
    }
    appendTracksToPlaylist(trackIds, targetPlaylistId);
}