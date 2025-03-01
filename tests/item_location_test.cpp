#include "catch/catch.hpp"

#include <functional>
#include <list>
#include <memory>
#include <string>

#include "avatar.h"
#include "game_constants.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "optional.h"
#include "point.h"
#include "rng.h"
#include "state_helpers.h"
#include "visitable.h"

TEST_CASE( "item_location_can_maintain_reference_despite_item_removal", "[item][item_location]" )
{
    clear_all_state();
    map &m = get_map();
    tripoint pos( 60, 60, 0 );
    m.i_clear( pos );
    m.add_item( pos, item( "jeans" ) );
    m.add_item( pos, item( "jeans" ) );
    m.add_item( pos, item( "tshirt" ) );
    m.add_item( pos, item( "jeans" ) );
    m.add_item( pos, item( "jeans" ) );
    map_cursor cursor( pos );
    item *tshirt = nullptr;
    cursor.visit_items( [&tshirt]( item * i ) {
        if( i->typeId() == itype_id( "tshirt" ) ) {
            tshirt = i;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } );
    REQUIRE( tshirt != nullptr );
    item_location item_loc( cursor, tshirt );
    REQUIRE( item_loc->typeId() == itype_id( "tshirt" ) );
    for( int j = 0; j < 4; ++j ) {
        // Delete up to 4 random jeans
        map_stack stack = m.i_at( pos );
        REQUIRE( !stack.empty() );
        item *i = &random_entry_opt( stack )->get();
        if( i->typeId() == itype_id( "jeans" ) ) {
            m.i_rem( pos, i );
        }
    }
    CAPTURE( m.i_at( pos ) );
    REQUIRE( item_loc );
    CHECK( item_loc->typeId() == itype_id( "tshirt" ) );
}

TEST_CASE( "item_location_doesnt_return_stale_map_item", "[item][item_location]" )
{
    clear_all_state();
    map &m = get_map();
    tripoint pos( 60, 60, 0 );
    m.i_clear( pos );
    m.add_item( pos, item( "tshirt" ) );
    item_location item_loc( map_cursor( pos ), &m.i_at( pos ).only_item() );
    REQUIRE( item_loc->typeId() == itype_id( "tshirt" ) );
    m.i_rem( pos, &*item_loc );
    m.add_item( pos, item( "jeans" ) );
    CHECK( !item_loc );
}

TEST_CASE( "item_in_container", "[item][item_location]" )
{
    clear_all_state();
    avatar &dummy = get_avatar();
    item &backpack = dummy.i_add( item( "backpack" ) );
    item jeans( "jeans" );

    REQUIRE( dummy.has_item( backpack ) );

    backpack.put_in( jeans );

    item_location backpack_loc( dummy, & **dummy.wear_possessed( backpack ) );

    REQUIRE( dummy.has_item( *backpack_loc ) );

    item_location jeans_loc( backpack_loc, &jeans );

    REQUIRE( backpack_loc.where() == item_location::type::character );
    REQUIRE( jeans_loc.where() == item_location::type::container );

    CHECK( backpack_loc.obtain_cost( dummy ) + INVENTORY_HANDLING_PENALTY == jeans_loc.obtain_cost(
               dummy ) );

    CHECK( jeans_loc.parent_item() == backpack_loc );
}
