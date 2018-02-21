create database if not exists AVRDoors
	default character set utf8
	DEFAULT COLLATE utf8_general_ci;

use AVRDoors;

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
	GroupID int unsigned not null,
	UserID int unsigned not null,
	-- If set can add/remove users from the group
	GroupAdmin bool not null default false,

	foreign key (GroupID) references Groups(GroupID),
	foreign key (UserID) references Users(UserID),
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
	ControllerID int unsigned not null,
	DoorIndex int unsigned not null,
	Location char(255) not null unique,
	OpeningDuration int unsigned,
	foreign key (ControllerID) references Controllers(ControllerID),
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
	Until datetime,
	DoorAdmin bool not null default false,

	foreign key (DoorID) references Doors(DoorID),
	foreign key (UserID) references Users(UserID),
	foreign key (GroupID) references Groups(GroupID)
);

-- The ACLs currently in the controllers
create table if not exists ControllerSetACL (
	ControllerID int unsigned not null,
	Card int unsigned,
	PIN char(8),
	Doors int unsigned not null,

	foreign key (ControllerID) references Controllers(ControllerID),
	unique key(ControllerID, Card, PIN)
);
-- BUG: The unique key(ControllerID, Card, PIN) doesn't work as NULL is
--      treated as unknown, so there can be multiple 'A NULL B' entries.
-- TODO: Add a trigger that check for this constraint using <=> as comparator.

-- All explicit access record for users
create or replace view UserAccess as
	select DoorID, UserID, PIN, Until
	from DoorAccess
	where GroupID is null;

-- All access records generated from the groups
create or replace view GroupAccess as
	select DoorID, GroupUsers.UserID, PIN, Until
	from DoorAccess
	join GroupUsers on DoorAccess.GroupID = GroupUsers.GroupID;

-- All access records from users and groups
-- Explicit user record always override those generated from groups
create or replace view AllAccess as
	select * from UserAccess
	union all
	(select GroupAccess.DoorID, GroupAccess.UserID,
		GroupAccess.PIN, GroupAccess.Until
	 from GroupAccess
	 left join UserAccess
	 on UserAccess.DoorID = GroupAccess.DoorID and
	    UserAccess.UserID = GroupAccess.UserID
	 where UserAccess.DoorID is null);

-- The ACL that should be in the controllers
create or replace view ControllerACL as
	select ControllerID, Card, PIN,
	       BIT_OR(1 << DoorIndex) as Doors
	from AllAccess
	left join Users on (AllAccess.UserID = Users.UserID)
	join Doors on (AllAccess.DoorID = Doors.DoorID)
	where (Card is not null -- Users with a card or PIN only
	       or (AllAccess.UserID is null and PIN is not null))
	  and (Until is null or Until > now())
	group by ControllerID, AllAccess.UserID, PIN;

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
