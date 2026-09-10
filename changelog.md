## v1.0.5

- Fixed post-completion rank popups using stale leaderboard data and showing incorrect small drops/gains such as -3, +1, or +2
- Rank freshness is now verified against the player's server-reported star count after completing a rated level
- Added automatic progressive retries when the leaderboard has not yet caught up or the player's score is missing from the response
- Added a silent rank baseline refresh on the main menu so the first rated completion after launch can display a rank change reliably
- Fixed stale/out-of-order leaderboard responses being able to overwrite newer rank data
- Fixed queued rank popups being lost or overwritten while another popup is animating or gameplay is active
- Fixed background rank refresh getting stuck after a level completion

## v1.0.4
- Zoomed logo in a bit more (last logo update)
- Added formatting to mod description
- Added repo link 
- Added Discord link (submit bugs here!)

## v1.0.3
- Fixed background refresh not grabbing new data making the popup not appear
- Added support for all platforms (Mac, Android, IOS)
- Updated the logo to have a thicker border 

## v1.0.2
- Public initial release
- Updated logo to be more contrasted
- Revised mod description and readme to be more accurate

## v1.0.1
- Updated the default value for popup background opacity
- Changed Geode tags from Utility and Interface to Enhancement and Online
- Forced minimum background refresh to prevent ratelimits

## v1.0.0
- Private release for testing