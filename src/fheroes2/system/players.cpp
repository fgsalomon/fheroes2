/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2019 - 2022                                             *
 *                                                                         *
 *   Free Heroes2 Engine: http://sourceforge.net/projects/fheroes2         *
 *   Copyright (C) 2011 by Andrey Afletdinov <fheroes2@gmail.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <algorithm>
#include <cassert>

#include "game.h"
#include "logging.h"
#include "maps_fileinfo.h"
#include "normal/ai_normal.h"
#include "players.h"
#include "race.h"
#include "save_format_version.h"
#include "serialize.h"
#include "settings.h"
#include "world.h"

namespace
{
    const int playersSize = KINGDOMMAX + 1;
    Player * _players[playersSize] = { nullptr };
    int human_colors = 0;

    enum
    {
        ST_INGAME = 0x2000
    };
}

void PlayerFocusReset( Player * player )
{
    if ( player )
        player->GetFocus().Reset();
}

void PlayerFixMultiControl( Player * player )
{
    if ( player && player->GetControl() == ( CONTROL_HUMAN | CONTROL_AI ) )
        player->SetControl( CONTROL_AI );
}

void PlayerRemoveAlreadySelectedRaces( const Player * player, std::vector<int> & availableRaces )
{
    const int raceToRemove = player->GetRace();
    availableRaces.erase( remove_if( availableRaces.begin(), availableRaces.end(), [raceToRemove]( const int race ) { return raceToRemove == race; } ),
                          availableRaces.end() );
}

void PlayerFixRandomRace( Player * player, std::vector<int> & availableRaces )
{
    if ( player && player->GetRace() == Race::RAND ) {
        if ( availableRaces.empty() ) {
            player->SetRace( Race::Rand() );
        }
        else {
            const int raceIndex = Rand::Get( 0, static_cast<uint32_t>( availableRaces.size() - 1 ) );
            player->SetRace( availableRaces[raceIndex] );
            availableRaces.erase( availableRaces.begin() + raceIndex );
        }
    }
}

bool Control::isControlAI() const
{
    return ( CONTROL_AI & GetControl() ) != 0;
}

bool Control::isControlHuman() const
{
    return ( CONTROL_HUMAN & GetControl() ) != 0;
}

bool Control::isControlLocal() const
{
    return !isControlRemote();
}

bool Control::isControlRemote() const
{
    return ( CONTROL_REMOTE & GetControl() ) != 0;
}

Player::Player( int col )
    : control( CONTROL_NONE )
    , color( col )
    , race( Race::NONE )
    , friends( col )
    , id( World::GetUniq() )
    , _ai( std::make_shared<AI::Normal>() )
    , _handicapStatus( HandicapStatus::NONE )
{}

std::string Player::GetDefaultName() const
{
    return Color::String( color );
}

std::string Player::GetName() const
{
    if ( name.empty() ) {
        return GetDefaultName();
    }

    return name;
}

int Player::GetControl() const
{
    return control;
}

std::string Player::GetPersonalityString() const
{
    return _ai->GetPersonalityString();
}

bool Player::isPlay() const
{
    return Modes( ST_INGAME );
}

void Player::SetName( const std::string & newName )
{
    if ( newName == GetDefaultName() ) {
        name.clear();
    }
    else {
        name = newName;
    }
}

void Player::SetPlay( bool f )
{
    if ( f )
        SetModes( ST_INGAME );
    else
        ResetModes( ST_INGAME );
}

void Player::setHandicapStatus( const uint8_t status )
{
    if ( status == HandicapStatus::NONE ) {
        _handicapStatus = status;
        return;
    }

    assert( control & CONTROL_HUMAN );

    _handicapStatus = status;
}

StreamBase & operator<<( StreamBase & msg, const Focus & focus )
{
    msg << focus.first;

    switch ( focus.first ) {
    case FOCUS_HEROES:
        msg << static_cast<Heroes *>( focus.second )->GetIndex();
        break;
    case FOCUS_CASTLE:
        msg << static_cast<Castle *>( focus.second )->GetIndex();
        break;
    default:
        msg << static_cast<int32_t>( -1 );
        break;
    }

    return msg;
}

StreamBase & operator>>( StreamBase & msg, Focus & focus )
{
    int32_t index;
    msg >> focus.first >> index;

    switch ( focus.first ) {
    case FOCUS_HEROES:
        focus.second = world.GetHeroes( Maps::GetPoint( index ) );
        break;
    case FOCUS_CASTLE:
        focus.second = world.getCastle( Maps::GetPoint( index ) );
        break;
    default:
        focus.second = nullptr;
        break;
    }

    return msg;
}

StreamBase & operator<<( StreamBase & msg, const Player & player )
{
    const BitModes & modes = player;

    assert( player._ai != nullptr );
    msg << modes << player.id << player.control << player.color << player.race << player.friends << player.name << player.focus << *player._ai << player._handicapStatus;
    return msg;
}

StreamBase & operator>>( StreamBase & msg, Player & player )
{
    BitModes & modes = player;

    msg >> modes >> player.id >> player.control >> player.color >> player.race >> player.friends >> player.name >> player.focus;

    assert( player._ai );
    msg >> *player._ai;

    if ( Game::GetLoadVersion() < FORMAT_VERSION_0920_RELEASE ) {
        player._handicapStatus = Player::HandicapStatus::NONE;
    }
    else {
        msg >> player._handicapStatus;
    }

    return msg;
}

Players::Players()
    : current_color( 0 )
{
    reserve( KINGDOMMAX );
}

Players::~Players()
{
    clear();
}

void Players::clear()
{
    for ( iterator it = begin(); it != end(); ++it )
        delete *it;

    std::vector<Player *>::clear();

    for ( uint32_t ii = 0; ii < KINGDOMMAX + 1; ++ii )
        _players[ii] = nullptr;

    current_color = 0;
    human_colors = 0;
}

void Players::Init( int colors )
{
    clear();

    const Colors vcolors( colors );

    for ( Colors::const_iterator it = vcolors.begin(); it != vcolors.end(); ++it ) {
        push_back( new Player( *it ) );
        _players[Color::GetIndex( *it )] = back();
    }

    DEBUG_LOG( DBG_GAME, DBG_INFO, "Players: " << String() )
}

void Players::Init( const Maps::FileInfo & fi )
{
    if ( fi.kingdom_colors ) {
        clear();
        const Colors vcolors( fi.kingdom_colors );

        Player * first = nullptr;

        for ( Colors::const_iterator it = vcolors.begin(); it != vcolors.end(); ++it ) {
            Player * player = new Player( *it );
            player->SetRace( fi.KingdomRace( *it ) );
            player->SetControl( CONTROL_AI );
            player->SetFriends( *it | fi.unions[Color::GetIndex( *it )] );

            if ( ( *it & fi.HumanOnlyColors() ) && Settings::Get().IsGameType( Game::TYPE_MULTI ) )
                player->SetControl( CONTROL_HUMAN );
            else if ( *it & fi.AllowHumanColors() )
                player->SetControl( player->GetControl() | CONTROL_HUMAN );

            if ( !first && ( player->GetControl() & CONTROL_HUMAN ) )
                first = player;

            push_back( player );
            _players[Color::GetIndex( *it )] = back();
        }

        if ( first )
            first->SetControl( CONTROL_HUMAN );

        DEBUG_LOG( DBG_GAME, DBG_INFO, "Players: " << String() )
    }
    else {
        DEBUG_LOG( DBG_GAME, DBG_INFO,
                   "Players: "
                       << "unknown colors" )
    }
}

void Players::Set( const int color, Player * player )
{
    assert( color >= 0 && color < playersSize );
    _players[color] = player;
}

Player * Players::Get( int color )
{
    return _players[Color::GetIndex( color )];
}

bool Players::isFriends( int player, int colors )
{
    const Player * ptr = Get( player );
    return ptr ? ( ptr->GetFriends() & colors ) != 0 : false;
}

void Players::SetPlayerRace( int color, int race )
{
    Player * player = Get( color );

    if ( player )
        player->SetRace( race );
}

void Players::SetPlayerControl( int color, int ctrl )
{
    Player * player = Get( color );

    if ( player )
        player->SetControl( ctrl );
}

int Players::GetColors( int control, bool strong ) const
{
    int res = 0;

    for ( const_iterator it = begin(); it != end(); ++it )
        if ( control == 0xFF || ( strong && ( *it )->GetControl() == control ) || ( !strong && ( ( *it )->GetControl() & control ) ) )
            res |= ( *it )->GetColor();

    return res;
}

int Players::GetActualColors() const
{
    int res = 0;

    for ( const_iterator it = begin(); it != end(); ++it )
        if ( ( *it )->isPlay() )
            res |= ( *it )->GetColor();

    return res;
}

const std::vector<Player *> & Players::getVector() const
{
    return *this;
}

Player * Players::GetCurrent()
{
    return Get( current_color );
}

const Player * Players::GetCurrent() const
{
    return Get( current_color );
}

int Players::GetPlayerFriends( int color )
{
    const Player * player = Get( color );
    return player ? player->GetFriends() : 0;
}

int Players::GetPlayerControl( int color )
{
    const Player * player = Get( color );
    return player ? player->GetControl() : CONTROL_NONE;
}

int Players::GetPlayerRace( int color )
{
    const Player * player = Get( color );
    return player ? player->GetRace() : Race::NONE;
}

bool Players::GetPlayerInGame( int color )
{
    const Player * player = Get( color );
    return player && player->isPlay();
}

void Players::SetPlayerInGame( int color, bool f )
{
    Player * player = Get( color );
    if ( player )
        player->SetPlay( f );
}

void Players::SetStartGame()
{
    vector<int> races = { Race::KNGT, Race::BARB, Race::SORC, Race::WRLK, Race::WZRD, Race::NECR };
    for_each( begin(), end(), []( Player * player ) { player->SetPlay( true ); } );
    for_each( begin(), end(), []( Player * player ) { PlayerFocusReset( player ); } );
    for_each( begin(), end(), [&races]( const Player * player ) { PlayerRemoveAlreadySelectedRaces( player, races ); } );
    for_each( begin(), end(), [&races]( Player * player ) { PlayerFixRandomRace( player, races ); } );
    for_each( begin(), end(), []( Player * player ) { PlayerFixMultiControl( player ); } );

    current_color = Color::NONE;
    human_colors = Color::NONE;

    DEBUG_LOG( DBG_GAME, DBG_INFO, String() )
}

int Players::HumanColors()
{
    if ( 0 == human_colors )
        human_colors = Settings::Get().GetPlayers().GetColors( CONTROL_HUMAN, true );
    return human_colors;
}

int Players::FriendColors()
{
    int colors = 0;
    const Players & players = Settings::Get().GetPlayers();

    if ( players.current_color & Players::HumanColors() ) {
        const Player * player = players.GetCurrent();
        if ( player )
            colors = player->GetFriends();
    }
    else
        colors = Players::HumanColors();

    return colors;
}

std::string Players::String() const
{
    std::ostringstream os;
    os << "Players: ";

    for ( const_iterator it = begin(); it != end(); ++it ) {
        os << Color::String( ( *it )->GetColor() ) << "(" << Race::String( ( *it )->GetRace() ) << ", ";

        switch ( ( *it )->GetControl() ) {
        case CONTROL_AI | CONTROL_HUMAN:
            os << "ai|human, " << ( *it )->GetPersonalityString();
            break;

        case CONTROL_AI:
            os << "ai, " << ( *it )->GetPersonalityString();
            break;

        case CONTROL_HUMAN:
            os << "human";
            break;

        case CONTROL_REMOTE:
            os << "remote";
            break;

        default:
            os << "unknown";
            break;
        }

        os << ")"
           << ", ";
    }

    return os.str();
}

StreamBase & operator<<( StreamBase & msg, const Players & players )
{
    msg << players.GetColors() << players.current_color;

    for ( Players::const_iterator it = players.begin(); it != players.end(); ++it )
        msg << ( **it );

    return msg;
}

StreamBase & operator>>( StreamBase & msg, Players & players )
{
    int colors;
    int current;
    msg >> colors >> current;

    players.clear();
    players.current_color = current;
    const Colors vcolors( colors );

    for ( uint32_t ii = 0; ii < vcolors.size(); ++ii ) {
        Player * player = new Player();
        msg >> *player;
        Players::Set( Color::GetIndex( player->GetColor() ), player );
        players.push_back( player );
    }

    return msg;
}
