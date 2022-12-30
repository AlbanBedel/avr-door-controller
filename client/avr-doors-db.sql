create database if not exists AVRDoors
	default character set utf8
	DEFAULT COLLATE utf8_general_ci;

use AVRDoors;

-- Functions needed for HOTP
drop function if exists HMAC_SHA1;
drop function if exists HMAC_PAD;
drop function if exists HMAC_PAD_FOUR;
drop function if exists HMAC_PAD_ONE;
drop function if exists HOTP;

DELIMITER //

/* process in 32-bit blocks to avoid bugs in older mysql versions */
CREATE FUNCTION HMAC_PAD_ONE(hexkey CHAR(128), key_offset INT, pad_data BIGINT UNSIGNED)
                RETURNS CHAR(8) DETERMINISTIC
BEGIN

RETURN LPAD(CONV(
              CONV(MID(hexkey, key_offset + 1, 8), 16, 10)  ^  pad_data,
              10, 16),
	    8, "0");

END //

CREATE FUNCTION HMAC_PAD_FOUR(hexkey CHAR(128), key_offset INT, pad_data BIGINT UNSIGNED)
                RETURNS CHAR(32) DETERMINISTIC
BEGIN

RETURN CONCAT(HMAC_PAD_ONE(hexkey, key_offset + 0, pad_data),
              HMAC_PAD_ONE(hexkey, key_offset + 8, pad_data),
              HMAC_PAD_ONE(hexkey, key_offset + 16, pad_data),
              HMAC_PAD_ONE(hexkey, key_offset + 24, pad_data));
END //

CREATE FUNCTION HMAC_PAD(hexkey CHAR(128), pad_data BIGINT UNSIGNED)
                RETURNS BINARY(64) DETERMINISTIC
BEGIN

RETURN UNHEX(CONCAT(HMAC_PAD_FOUR(hexkey, 0, pad_data),
                    HMAC_PAD_FOUR(hexkey, 32, pad_data),
                    HMAC_PAD_FOUR(hexkey, 64, pad_data),
                    HMAC_PAD_FOUR(hexkey, 96, pad_data)));
END //

CREATE FUNCTION HMAC_SHA1(secret VARBINARY(64), text VARBINARY(128))
                RETURNS CHAR(40) DETERMINISTIC
BEGIN
DECLARE ipad, opad BINARY(64);
DECLARE hexkey CHAR(128);

SET hexkey = RPAD(HEX(secret), 128, "0");
SET ipad = HMAC_PAD(hexkey, 0x36363636);
SET opad = HMAC_PAD(hexkey, 0x5c5c5c5c);

RETURN SHA1(CONCAT(opad, UNHEX(SHA1(CONCAT(ipad, text)))));

END //

CREATE FUNCTION HOTP(secret VARBINARY(64), cnt BIGINT UNSIGNED, digits INT UNSIGNED)
                RETURNS VARCHAR(10) DETERMINISTIC
BEGIN
DECLARE hmac CHAR(40);
DECLARE offset INT UNSIGNED;
DECLARE bin_code INT UNSIGNED;

SET hmac = HMAC_SHA1(secret, UNHEX(LPAD(HEX(cnt), 16, '0')));
SET offset = CONV(MID(hmac, 1 + 19*2 + 1, 1), 16, 10);
SET bin_code = CONV(CONCAT(CONV(CONV(MID(hmac, 1 + offset * 2, 2), 16, 10) & 0x7F, 10, 16),
                           MID(hmac, 1 + (offset + 1) * 2, 6)),
	            16, 10);

RETURN LPAD(bin_code % POW(10, digits), digits, '0');

END //

DELIMITER ;

-- Create the basic tables
create table if not exists Users (
	UserID int unsigned auto_increment primary key,
	CreatedOn datetime not null default current_timestamp,
	LastModified datetime not null default current_timestamp
					on update current_timestamp,
	UserName char(128) unique,
	Password char(128),
	EMail char(128),
	Phone char(64),
	-- allow null to support users who lost their card
	-- without removing their permissions
	Card int unsigned unique
);

create table if not exists Groups (
	GroupID int unsigned auto_increment primary key,
	CreatedOn datetime not null default current_timestamp,
	LastModified datetime not null default current_timestamp
					on update current_timestamp,
	GroupName char(128) not null unique,
	EMail char(128)
);

create table if not exists GroupUsers (
	CreatedOn datetime not null default current_timestamp,
	LastModified datetime not null default current_timestamp
					on update current_timestamp,
	GroupID int unsigned not null,
	UserID int unsigned not null,
	-- If set can add/remove users from the group
	GroupAdmin bool not null default false,

	foreign key (GroupID) references Groups(GroupID)
		on update cascade on delete cascade,
	foreign key (UserID) references Users(UserID)
		on update cascade on delete cascade,
	primary key(GroupID, UserID)
);

create table if not exists Controllers (
	ControllerID int unsigned auto_increment primary key,
	CreatedOn datetime not null default current_timestamp,
	LastModified datetime not null default current_timestamp
					on update current_timestamp,
	Location char(255) not null unique,
	URL char(255) unique,
	Username char(64),
	Password char(128),
	Firmware char(64),
	MaxACL int unsigned
);

create table if not exists Doors (
	DoorID int unsigned auto_increment primary key,
	CreatedOn datetime not null default current_timestamp,
	LastModified datetime not null default current_timestamp
					on update current_timestamp,
	ControllerID int unsigned,
	DoorIndex int unsigned,
	Location char(255) not null unique,
	OpeningDuration int unsigned,
	foreign key (ControllerID) references Controllers(ControllerID)
		on update cascade on delete set null,
	unique key (ControllerID, DoorIndex)
);

create table if not exists DoorAccess (
	DoorAccessID int unsigned auto_increment primary key,
	CreatedOn datetime not null default current_timestamp,
	LastModified datetime not null default current_timestamp
					on update current_timestamp,
	DoorID int unsigned not null,
	UserID int unsigned,
	GroupID int unsigned,
	PIN char(8),
	Since datetime,
	Until datetime,
	DoorAdmin bool not null default false,

	foreign key (DoorID) references Doors(DoorID)
		on update cascade on delete cascade,
	foreign key (UserID) references Users(UserID)
		on update cascade on delete cascade,
	foreign key (GroupID) references Groups(GroupID)
		on update cascade on delete cascade
);

create table if not exists DoorOTP (
	DoorOTPID int unsigned auto_increment primary key,
	CreatedOn datetime not null default current_timestamp,
	LastModified datetime not null default current_timestamp
					on update current_timestamp,
	DoorID int unsigned not null,
	Secret binary(64) not null,
	Digits int unsigned not null default 6,
	Period int unsigned not null default 86400,

	foreign key (DoorID) references Doors(DoorID)
		on update cascade on delete cascade
);

create table if not exists DoorOTPOffset (
	DoorOTPID int unsigned not null,
	Offset int not null default 0,

	foreign key (DoorOTPID) references DoorOTP(DoorOTPID)
		on update cascade on delete cascade,
	unique key(DoorOTPID, Offset)
);

create or replace view DoorOTPPin as
	select DoorID, HOTP(Secret, floor(unix_timestamp() / Period) + Offset, Digits) as PIN
	from DoorOTPOffset
	left join DoorOTP on (DoorOTPOffset.DoorOTPID = DoorOTP.DoorOTPID);

-- The ACLs currently in the controllers
create table if not exists ControllerSetACL (
	ControllerID int unsigned not null,
	Card int unsigned,
	PIN char(8),
	Doors int unsigned not null,

	foreign key (ControllerID) references Controllers(ControllerID)
		on update cascade on delete cascade,
	unique key(ControllerID, Card, PIN)
);
-- BUG: The unique key(ControllerID, Card, PIN) doesn't work as NULL is
--      treated as unknown, so there can be multiple 'A NULL B' entries.
-- TODO: Add a trigger that check for this constraint using <=> as comparator.

-- All explicit access record for users
create or replace view UserAccess as
	select DoorAccessID, DoorID, UserID, PIN, Since, Until, DoorAdmin
	from DoorAccess
	where GroupID is null;

-- All access records generated from the groups
create or replace view GroupAccess as
	select DoorAccessID, DoorID, GroupUsers.UserID, PIN, Since, Until, DoorAdmin
	from DoorAccess
	join GroupUsers on DoorAccess.GroupID = GroupUsers.GroupID;

-- All access records from users and groups
-- Explicit user record always override those generated from groups
create or replace view AllAccess as
	select * from UserAccess
	union all
	(select GroupAccess.DoorAccessID,
		GroupAccess.DoorID, GroupAccess.UserID,
		GroupAccess.PIN, GroupAccess.Since, GroupAccess.Until,
		GroupAccess.DoorAdmin
	 from GroupAccess
	 left join UserAccess
	 on UserAccess.DoorID = GroupAccess.DoorID and
	    UserAccess.UserID = GroupAccess.UserID
	 where UserAccess.DoorID is null);

-- Generate an ACL list for the users and groups
create or replace view UsersACL as
	select ControllerID, Card, PIN,
	       BIT_OR(1 << DoorIndex) as Doors
	from AllAccess
	left join Users on (AllAccess.UserID = Users.UserID)
	join Doors on (AllAccess.DoorID = Doors.DoorID)
	where ControllerID is not null and DoorIndex is not null
	  and (Card is not null -- Users with a card or PIN only
	       or (AllAccess.UserID is null and PIN is not null))
	  and (Since is null or Since <= now())
	  and (Until is null or Until > now())
	group by ControllerID, AllAccess.UserID, PIN;

-- Generate an ACL list for the OTP pins
create or replace view DoorOTPACL as
	select ControllerID, PIN,
	       BIT_OR(1 << DoorIndex) as Doors
	from DoorOTPPin
	join Doors on (DoorOTPPin.DoorID = Doors.DoorID)
	where ControllerID is not null and DoorIndex is not null
	group by ControllerID, PIN;

-- The ACL that should be in the controllers
create or replace view ControllerACL as
	select ControllerID, Card, PIN, BIT_OR(Doors) as Doors
	from (select * from UsersACL
	      union all
	      (select ControllerID, NULL, PIN, Doors from DoorOTPACL)
	) t
	group by ControllerID, Card, PIN;

-- The differences between what is and what should be in the controllers.
-- The 'Op' column indicate if the entry should be added (1) or removed (0).
create or replace view ControllerChanges as
	select ControllerID, sum(op) as Op,
	       Card, PIN, Doors
	from (select ControllerID, Card, PIN, 0 as Op, Doors
	      from ControllerSetACL
	      union all
	      select ControllerID, Card, PIN, 1 as Op, Doors
	      from ControllerACL) ACL
	group by ControllerID, Card, PIN, Doors
	having count(*) = 1
	order by ControllerID, sum(Op), Card, PIN;

-- The updates that should be done to the controllers with the new
-- Doors value for each entry. If Doors is 0 the entry should be removed.
create or replace view ControllerACLUpdates as
	select ControllerID, Card, PIN, sum(Doors * Op) as Doors
	from ControllerChanges
	group by ControllerID, Card, PIN
	order by ControllerID, Doors, Card, PIN;
