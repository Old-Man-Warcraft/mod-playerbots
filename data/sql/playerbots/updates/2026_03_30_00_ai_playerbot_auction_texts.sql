DELETE FROM ai_playerbot_texts WHERE name IN (
    'auction_no_auctioneers_nearby',
    'auction_sell_usage_error',
    'travel_target_activity_auction_house',
    'auction_buy_success',
    'auction_sell_success'
);

DELETE FROM ai_playerbot_texts_chance WHERE name IN (
    'auction_no_auctioneers_nearby',
    'auction_sell_usage_error',
    'travel_target_activity_auction_house',
    'auction_buy_success',
    'auction_sell_success'
);

INSERT INTO ai_playerbot_texts (
    id, name, text, say_type, reply_type,
    text_loc1, text_loc2, text_loc3, text_loc4, text_loc5, text_loc6, text_loc7, text_loc8
) VALUES
(3781, 'auction_no_auctioneers_nearby', 'There are no auctioneers nearby', 0, 0,
'', '', '', '', '', '', '', ''),

(3782, 'auction_sell_usage_error', 'Usage: s gray/*/vendor/auction/[item link]', 0, 0,
'', '', '', '', '', '', '', ''),

(3783, 'travel_target_activity_auction_house', 'auction house', 0, 0,
'', '', '', '', '', '', '', ''),

(3784, 'auction_buy_success', 'Buying from auction house %item_link for %cost', 0, 0,
'', '', '', '', '', '', '', ''),

(3785, 'auction_sell_success', 'Posting to auction house %item_link for %start_bid..%buyout', 0, 0,
'', '', '', '', '', '', '', '');

INSERT INTO ai_playerbot_texts_chance (name, probability) VALUES
('auction_no_auctioneers_nearby', 100),
('auction_sell_usage_error', 100),
('travel_target_activity_auction_house', 100),
('auction_buy_success', 100),
('auction_sell_success', 100);
