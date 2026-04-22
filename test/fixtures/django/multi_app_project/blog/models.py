from django.db import models


class Post(models.Model):
    author = models.ForeignKey("accounts.Author", on_delete=models.CASCADE)
    tags = models.ManyToManyField("taxonomy.Tag", related_name="posts")
    title = models.CharField(max_length=200)
