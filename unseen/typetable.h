#ifndef _TYPETABLE_H_
#define _TYPETABLE_H_

#define NUM_PRIMTYPES 9
#define VARIABLESIZE -1

extern int primtype_val[];

#define PRIMTYPE_BOOLEAN 	&primtype_val[0]
#define PRIMTYPE_INTEGER	&primtype_val[1]
#define PRIMTYPE_REAL		&primtype_val[2]
#define PRIMTYPE_CHARACTER	&primtype_val[3]

#define PRIMTYPE_VARCHAR	&primtype_val[4]
#define PRIMTYPE_BLOB		&primtype_val[5]

#define PRIMTYPE_TINYINT	&primtype_val[6]
#define PRIMTYPE_SMALLINT	&primtype_val[7]

#define PRIMTYPE_TIMESTAMP	&primtype_val[8]

extern const int primtype_size[];

extern const char *primtype_name[];

/*
 * looks up the string type name and returns the corresponding array index
 * returns -1 if not found
 */
int typetable_index(char *name);

#endif /* _TYPETABLE_H_ */
